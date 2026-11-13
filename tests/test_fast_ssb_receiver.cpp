#include "fast/ssb_receiver.hpp"
#include "fast/dense_acoustic_receiver.hpp"
#include "fast_test_key.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/simulation.hpp"
#include "datapump/resampler.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace {
namespace fast = datapump::fast;
using namespace datapump::fast::detail;

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

using Interval=std::array<std::uint8_t,fast::physical_interval_bits>;
using Soft=std::array<float,fast::physical_interval_bits>;

struct RawTransmission {
    std::vector<Interval> bits;
    std::vector<float> pcm;
};

RawTransmission render(const fast::Profile& profile,std::size_t count,unsigned seed) {
    RawTransmission result{std::vector<Interval>(count),
        std::vector<float>(static_cast<std::size_t>(fast::transmission_samples(profile,count)))};
    std::mt19937 random(seed);
    for(auto& interval:result.bits)
        for(auto& bit:interval)bit=static_cast<std::uint8_t>(random()&1U);
    std::size_t sent=0;
    fast::Transmitter tx(profile,[&](std::span<std::uint8_t> interval) {
        if(sent==count)return false;
        std::ranges::copy(result.bits[sent++],interval.begin());
        return true;
    });
    check(tx.read(result.pcm)==result.pcm.size(),"Raw SSB fixture lost its transmitted audio");
    return result;
}

void check_encrypted_stream() {
    auto profile=fast::capacity_profile(fast::Channel::ssb);
    // Keep the full clean integration fixture short with a local higher-order
    // override. It still uses the canonical SSB pilot sequence and identity.
    profile.constellation=64;
    profile.ldpc_frame_bits=64800;
    profile.code_rate=fast::CodeRate::three_quarters;
    profile.pilot_spacing_symbols=64;
    profile.interleave_depth=1;
    const auto key=fast_test::sequence_key();
    constexpr std::string_view filename="ssb-candidate.bin";
    constexpr std::uint64_t quota=1ULL<<20;
    datapump::Bytes source(321);
    for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<std::uint8_t>(i*37+i/11);
    auto encoded=fast::prepare_xz_attachment(fast::byte_source(source),filename,quota);
    auto encoder=fast::testing::deterministic_encoder(profile,key,
        fast::byte_source(std::move(encoded.encoded)),0x187b,fast::SourceEncoding::xz);
    fast::Transmitter tx(profile,[&](std::span<std::uint8_t> bits){return encoder.next_interval(bits);});
    fast::StreamDecoder decoder(profile,key,quota,fast::SourceEncoding::xz);
    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    std::size_t feedback_events=0;
    std::size_t observed_symbols=0,observed_input=0;
    SsbReceiver receiver(profile,geometry,bounds,.10,
        [&](std::span<const float> interval,bool feedback) -> std::optional<fast::ValidatedCapacityCycle> {
            if(feedback) {
                auto event=decoder.push_interval_with_feedback(interval);
                if(event)++feedback_events;
                return event;
            }
            decoder.push_interval(interval);
            return {};
        },{},SsbReceiverModelPolicy::sparse_delay_doppler,
        [&](std::complex<float>) {
            if(++observed_symbols==1)throw std::runtime_error("Test symbol display failure");
        },[&](std::complex<float>) {
            if(++observed_input==1)throw std::runtime_error("Test input display failure");
        });
    std::vector<std::pair<std::uint64_t,SsbCycleObservation>> cycle_observations;
    receiver.set_cycle_observer([&](std::uint64_t cycle,SsbCycleObservation stage) {
        cycle_observations.emplace_back(cycle,stage);
        if(cycle_observations.size()==1)throw std::runtime_error("Test cycle observation failure");
    });

    std::array<float,1023> pcm{};
    std::size_t cropped=profile.sample_rate/20;
    while(!tx.finished()) {
        const auto count=tx.read(pcm);
        const auto drop=std::min(cropped,count);
        cropped-=drop;
        const auto input=std::span(pcm).subspan(drop,count-drop);
        check(receiver.push(input)==input.size(),"Fast SSB receiver stopped during transmitted audio");
        check(!receiver.progress().physical_complete && !decoder.result(),
              "Fast SSB exposed a physical end during continuing audio");
    }
    check(!cropped && receiver.progress().acquired,"Fast SSB receiver did not acquire a clipped active start");

    pcm.fill(0.F);
    const auto quiet=[&](std::uint64_t samples) {
        while(samples && !receiver.progress().physical_complete) {
            const auto count=std::min<std::uint64_t>(samples,pcm.size());
            check(receiver.push(std::span(pcm).first(count))==count,"Fast SSB stopped before consuming a quiet block");
            samples-=count;
        }
    };
    const auto short_tail=profile.sample_rate*11/2;
    quiet(short_tail);
    check(!receiver.progress().physical_complete && !decoder.result(),
          "Fast SSB counted fewer than six seconds of physical quiet");
    quiet(fast::end_silence_samples(profile)-short_tail);
    const auto physical_end=receiver.progress().physical_complete;
    receiver.finish();
    check(physical_end && receiver.completion_tick(),"Fast SSB did not score the observed physical tail");
    check(!decoder.result(),"Fast SSB decoder published before DSP completion was delivered");
    decoder.finish(physical_end);
    const auto file=decoder.result();
    const auto stats=decoder.snapshot();
    check(file && file->is_attachment() && file->filename()==filename && stats.authenticated &&
          std::ranges::equal(file->bytes(),source),
          "Fast SSB composed receiver did not recover the encrypted XZ attachment");
    check(stats.intervals==encoder.intervals_emitted() && !receiver.progress().erased_intervals,
          "Fast SSB counted trailing quiet as source intervals");
    check(feedback_events>=1,"Fast SSB composed receiver did not deliver a validated cycle");
    const std::array expected_cycles{
        std::pair{std::uint64_t{0},SsbCycleObservation::before_refinement},
        std::pair{std::uint64_t{0},SsbCycleObservation::after_refinement},
        std::pair{std::uint64_t{1},SsbCycleObservation::before_refinement},
        std::pair{std::uint64_t{1},SsbCycleObservation::after_refinement}};
    check(std::ranges::equal(cycle_observations,expected_cycles),"Fast SSB cycle observation lost its native boundaries");
    check(observed_symbols && observed_symbols<=stats.intervals*512 && observed_input>observed_symbols,
          "Fast SSB lost bounded actual symbol or input display observations");
    const auto& refinement=receiver.refinement_progress();
    check(refinement.attempted && refinement.attempted<=feedback_events &&
          refinement.accepted<=refinement.attempted && refinement.rate_updates<=refinement.accepted &&
          refinement.first_accepted_wire_ordinal.has_value()==bool(refinement.accepted),
          "Fast SSB lost cycle refinement decisions");
    std::cout<<"intervals="<<stats.intervals<<" authenticated_areas="<<stats.authenticated_groups
             <<" feedback_events="<<feedback_events<<" completion_tick="<<*receiver.completion_tick()
             <<" refinement_attempted="<<refinement.attempted<<" refinement_accepted="<<refinement.accepted
             <<" refinement_rate_updates="<<refinement.rate_updates
             <<" receiver_workspace_bytes="<<receiver.workspace_bytes();
    if(refinement.first_accepted_wire_ordinal)
        std::cout<<" first_accepted_wire_ordinal="<<*refinement.first_accepted_wire_ordinal;
    std::cout<<'\n';
}

void check_interior_hole() {
    auto profile=fast::capacity_profile(fast::Channel::ssb);
    profile.ldpc_frame_bits=64800;
    profile.constellation=4;
    profile.code_rate=fast::CodeRate::half;
    profile.interleave_depth=1;
    profile.marker_spacing_intervals=1;
    profile.pilot_spacing_symbols=64;
    constexpr std::size_t count=5, missing=2;
    auto [source,waveform]=render(profile,count,105);
    const auto symbol_time=profile.sample_rate/profile.symbol_rate;
    const auto group_delay=std::ceil(6.4/profile.rolloff);
    const auto first_symbol=fast::preamble_symbols(profile)+fast::total_interval_symbols(profile,missing);
    const auto last_symbol=fast::preamble_symbols(profile)+fast::total_interval_symbols(profile,missing+1);
    const auto first=static_cast<std::size_t>((first_symbol+group_delay-24)*symbol_time);
    const auto last=static_cast<std::size_t>(std::ceil((last_symbol+group_delay+28)*symbol_time));
    std::fill(waveform.begin()+static_cast<std::ptrdiff_t>(first),
              waveform.begin()+static_cast<std::ptrdiff_t>(last),0.F);

    std::vector<Soft> received;
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    SsbReceiver receiver(profile,geometry,bounds,.10,
        [&](std::span<const float> interval,bool feedback) -> std::optional<fast::ValidatedCapacityCycle> {
            check(!feedback&&received.size()<count,"Timed-hole fixture produced unsolicited output");
            std::ranges::copy(interval,received.emplace_back().begin());
            return {};
        });
    constexpr std::size_t chunk=1023;
    for(std::size_t offset=0;offset<waveform.size();offset+=chunk) {
        const auto input=std::span(waveform).subspan(offset,std::min(chunk,waveform.size()-offset));
        check(receiver.push(input)==input.size() && !receiver.progress().physical_complete,
              "An interior timed hole ended a continuing transmission");
    }
    std::array<float,chunk> quiet{};
    auto remaining=fast::end_silence_samples(profile);
    while(remaining && !receiver.progress().physical_complete) {
        const auto samples=std::min<std::uint64_t>(remaining,quiet.size());
        check(receiver.push(std::span(quiet).first(samples))==samples,"Timed-hole quiet audio was truncated");
        remaining-=samples;
    }
    receiver.finish();
    check(receiver.progress().physical_complete && received.size()==count,
          "Timed-hole fixture changed the number of committed intervals");
    check(std::ranges::all_of(received[missing],[](float value){return value==0.F;}) &&
          receiver.progress().erased_intervals>=1,"Interior hole did not retain neutral evidence at its ordinal");
    for(std::size_t i=0;i<source.back().size();++i)
        check(received.back()[i]!=0.F && (received.back()[i]>0.F)==bool(source.back()[i]),
              "A later interval failed to recover at its original ordinal");
}

double colored_interference(std::span<float> pcm,const fast::Profile& profile,unsigned seed) {
    std::mt19937 random(seed);
    std::normal_distribution<double> normal;
    std::complex<double> state{};
    constexpr double pole=.90;
    const auto innovation=std::sqrt(1.-pole*pole);
    const auto carrier_step=std::polar(1.,2*std::numbers::pi*profile.carrier_hz/profile.sample_rate);
    const auto tone_step=std::polar(1.,2*std::numbers::pi*(profile.carrier_hz+438.)/profile.sample_rate);
    std::complex<double> carrier=1., tone=1.;
    double peak=0.;
    for(std::size_t i=0;i<pcm.size();++i) {
        state=pole*state+innovation*std::complex<double>(normal(random),normal(random));
        const auto gain=i<2*profile.sample_rate?.06:.16;
        const auto value=gain*(state*carrier).real()+.28*tone.real();
        pcm[i]=static_cast<float>(value);
        peak=std::max(peak,std::abs(value));
        carrier*=carrier_step;
        tone*=tone_step;
    }
    return peak;
}

double low_level_noise(std::span<float> pcm,unsigned seed) {
    std::mt19937 random(seed);
    std::normal_distribution<float> normal(0.F,.00535F);
    double peak=0.;
    for(auto& sample:pcm) {
        sample=normal(random);
        peak=std::max(peak,std::abs(double(sample)));
    }
    return peak;
}

std::vector<float> one_symbol_echo(std::span<const float> waveform,const fast::Profile& profile) {
    const auto delay=static_cast<std::size_t>(std::round(profile.sample_rate/profile.symbol_rate));
    std::vector<float> echo(waveform.size()+delay);
    for(std::size_t i=0;i<waveform.size();++i) {
        echo[i]+=.65F*waveform[i];
        echo[i+delay]+=.23F*waveform[i];
    }
    return echo;
}

void print_quiet(const SsbReceiver& receiver) {
    const auto& quiet=receiver.quiet_progress();
    const auto range=[](const auto& span) {
        if(span)std::cout<<'['<<span->first<<','<<span->past_last<<')';
        else std::cout<<"none";
    };
    const auto scores=[&](const char* name,const SsbQuietScoreProgress& score) {
        std::cout<<' '<<name<<'='<<score.units<<" last=";
        range(score.last);
        std::cout<<" observable="<<score.last_observable_groups<<'/'<<score.last_groups
                 <<" squared_coherence="<<score.last_squared_coherence
                 <<" projected_to_raw="<<score.last_projected_to_raw_energy;
    };
    std::cout<<"longest_quiet_ticks="<<quiet.longest_quiet_ticks<<" current=";
    range(quiet.current_quiet_run);
    std::cout<<" pending=";
    range(quiet.pending_observation);
    std::cout<<" last_predictive_payload=";
    range(quiet.last_predictive_payload);
    std::cout<<" quiet_eligible_after_tick="<<quiet.quiet_eligible_after_tick.value_or(0);
    std::cout<<" unscored_payload_intervals="<<quiet.unscored_payload_intervals
             <<" no_detection_unobservable_units="<<quiet.no_detection_unobservable_units
             <<" low_power_calibrated_units="<<quiet.low_power_calibrated_units
             <<" low_power_coherent_veto_units="<<quiet.low_power_coherent_veto_units
             <<" low_power_ceiling="<<quiet.low_power_ceiling.value_or(0.);
    scores("absent",quiet.absent);
    scores("present",quiet.present);
    scores("no_detection",quiet.no_detection);
    scores("unscored",quiet.unscored);
    std::cout<<std::endl;
}

void check_hf_quiet() {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    constexpr std::size_t count=2;
    std::array<Interval,count> bits{};
    std::mt19937 random(4109);
    for(auto& interval:bits)
        for(auto& bit:interval)bit=static_cast<std::uint8_t>(random()&1U);
    std::size_t sent=0;
    fast::Transmitter tx(profile,[&](std::span<std::uint8_t> out) {
        if(sent==count)return false;
        std::ranges::copy(bits[sent++],out.begin());
        return true;
    });
    const auto base=datapump::tuning::parse_oscillator_preset("crystal");
    const auto shift=datapump::tuning::parse_oscillator_preset("ic-7100");
    const auto spread=datapump::tuning::parse_spread_preset("hf-worst");
    constexpr auto radians=std::numbers::pi/180.;
    fast::SimulationSettings settings;
    settings.transmit_watts=40.;
    settings.path_loss_db=130.;
    settings.noise_dbm_hz=-174.;
    settings.baseband_ppm=base.clock_error_ppm;
    settings.baseband_phase_rad_sqrt_s=base.phase_noise_degrees_per_sqrt_second*radians;
    settings.shift_ppm=shift.clock_error_ppm;
    settings.shift_phase_rad_sqrt_s=shift.phase_noise_degrees_per_sqrt_second*radians;
    settings.shift_hz=30e6;
    settings.delay_ms=spread.delay_ms;
    settings.doppler_hz=spread.doppler_hz;
    settings.selective_slow_depth=.65;
    settings.selective_fast_depth=.65;
    settings.margin=3.;
    settings.seed=417;
    fast::SimulationChannel channel(profile,settings,[&](auto samples){return tx.read_analytic(samples);});
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    std::size_t received=0,hard_errors=0;
    SsbReceiver receiver(profile,make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz),bounds,.10,
        [&](std::span<const float> soft,bool feedback)->std::optional<fast::ValidatedCapacityCycle> {
            check(!feedback && received<count,"HF quiet control committed a phantom interval");
            for(std::size_t i=0;i<soft.size();++i)hard_errors+=(soft[i]>0.F)!=bool(bits[received][i]);
            ++received;
            return {};
        });
    std::array<float,4096> pcm{};
    while(!receiver.progress().physical_complete) {
        const auto samples=channel.read(pcm);
        if(!samples)break;
        const auto consumed=receiver.push(std::span(pcm).first(samples));
        check(consumed==samples || receiver.progress().physical_complete,"HF quiet control dropped real PCM");
    }
    receiver.finish();
    const auto start=channel.report().quiet_start_sample;
    const auto end=receiver.completion_tick();
    std::cout<<"hf_seed=417 raw_intervals="<<received<<" hard_bit_errors="<<hard_errors
             <<" actual_output_ticks="<<channel.report().output_samples<<" physical_tail_origin="<<start.value_or(0)
             <<" physical_completion_tick="<<end.value_or(0)<<'\n';
    print_quiet(receiver);
    check(receiver.progress().acquired && received==count && !receiver.progress().erased_intervals,
          "HF quiet control lacked its two tracked source intervals");
    check(start && end && *end>=*start+6ULL*profile.sample_rate,
          "HF quiet control lacked six observed seconds after the propagated waveform");
}

void check_narrow_acoustic() {
    auto profile=fast::capacity_profile(fast::Channel::acoustic_short);
    profile.acoustic_ofdm=false;
    profile.ofdm_training_blocks=16;
    profile.ldpc_frame_bits=1944;
    profile.constellation=4;
    profile.carrier_hz=1800.;
    profile.symbol_rate=183.;
    profile.amplitude*=std::sqrt(2.)/4.5;
    check(supports_delay_doppler_receiver(profile),"Narrow acoustic waveform lost delay/Doppler admission");
    auto below=profile;
    below.symbol_rate=179.;
    check(!supports_delay_doppler_receiver(below),"Narrow acoustic fit exceeded its bounded rate range");

    constexpr std::uint64_t quota=1ULL<<20;
    const datapump::Bytes source{'f','o','x'};
    auto encoded=fast::prepare_xz_source(fast::byte_source(source),quota);
    auto encoder=fast::testing::deterministic_encoder(profile,std::nullopt,
        fast::byte_source(std::move(encoded.encoded)),0x439a,fast::SourceEncoding::xz);
    fast::Transmitter transmitter(profile,[&](auto bits){return encoder.next_interval(bits);});
    const auto base=datapump::tuning::parse_oscillator_preset("crystal");
    const auto shift=datapump::tuning::parse_oscillator_preset("ic-7100");
    constexpr auto radians=std::numbers::pi/180.;
    fast::SimulationSettings settings;
    settings.baseband_ppm=base.clock_error_ppm;
    settings.baseband_phase_rad_sqrt_s=base.phase_noise_degrees_per_sqrt_second*radians;
    settings.shift_ppm=shift.clock_error_ppm;
    settings.shift_phase_rad_sqrt_s=shift.phase_noise_degrees_per_sqrt_second*radians;
    settings.shift_hz=30e6;
    settings.delay_ms=7.;
    settings.doppler_hz=30.;
    settings.selective_slow_depth=.65;
    settings.selective_fast_depth=.65;
    settings.seed=419;
    // A white receive floor referred to the nominal 17.5 kHz acoustic band.
    // Use a moderate noise holdout in addition to the modeled moving rays.
    constexpr double cn0_db_hz=40.;
    settings.noise_dbm_hz=-174.;
    settings.path_loss_db=30.+10.*std::log10(settings.transmit_watts)-*settings.noise_dbm_hz-cn0_db_hz;
    fast::SimulationChannel channel(profile,settings,[&](auto pcm){return transmitter.read_analytic(pcm);});
    fast::StreamDecoder decoder(profile,std::nullopt,quota,fast::SourceEncoding::xz);
    fast::Receiver receiver(profile,[&](auto bits,bool feedback) {
        return decoder.push_interval(bits,feedback);
    },{},{},{});
    std::array<float,4093> pcm{};
    while(!receiver.progress().physical_complete) {
        const auto count=channel.read(pcm);
        if(!count)break;
        receiver.push(std::span(pcm).first(count));
        check(!decoder.result(),"Narrow acoustic decoder published before physical completion");
    }
    receiver.finish();
    const auto completion=receiver.physical_completion_input_tick();
    const auto quiet=channel.report().quiet_start_sample;
    decoder.finish(receiver.progress().physical_complete);
    const auto result=decoder.result();
    const auto decoded=decoder.snapshot();
    std::cout<<"narrow_acoustic rate="<<profile.symbol_rate<<" cn0_db_hz="<<cn0_db_hz
             <<" backend="<<receiver.backend_name()<<" intervals="<<decoded.intervals
             <<" ldpc_failed="<<decoded.ldpc_failed_frames<<" physical="<<receiver.progress().physical_complete
             <<" workspace_bytes="<<receiver.workspace_bytes()<<" quiet_start="<<quiet.value_or(0)
             <<" completion="<<completion.value_or(0)<<" generated="<<channel.report().output_samples<<'\n';
    if(const auto* sparse=ReceiverDiagnostics::ssb(receiver))print_quiet(*sparse);
    check(receiver.uses_delay_doppler()&&completion&&quiet&&*completion>=*quiet+6ULL*profile.sample_rate,
          "Narrow acoustic waveform did not score the actual six-second tail");
    check(result&&std::ranges::equal(result->bytes(),source)&&decoded.ldpc_frames==2&&!decoded.ldpc_failed_frames,
          "Narrow acoustic delay/Doppler reception lost the protected source");
    check(receiver.workspace_bytes()<64U*1024U*1024U,"Narrow acoustic receiver exceeded bounded workspace");

    unsigned intervals=0;
    fast::Receiver noise_only(profile,[&](auto,bool) -> std::optional<fast::ValidatedCapacityCycle> {
        ++intervals;return {};
    },{},{},{});
    std::mt19937 random(479);
    std::normal_distribution<float> noise(0.F,static_cast<float>(std::sqrt(channel.report().noise_variance)));
    for(auto left=10ULL*profile.sample_rate;left;) {
        const auto count=std::min<std::uint64_t>(left,pcm.size());
        const auto input=std::span(pcm).first(count);
        for(auto& sample:input)sample=noise(random);
        noise_only.push(input);
        left-=count;
    }
    const auto* model=ReceiverDiagnostics::ssb(noise_only);
    check(model&&model->acquisition_progress().fit_requests==0&&
          !noise_only.progress().acquired&&!noise_only.progress().physical_complete&&!intervals,
          "Narrow acoustic noise triggered a full fit or manufactured reception");
}

void check_narrow_acoustic_acquisition_grace() {
    auto profile=fast::capacity_profile(fast::Channel::acoustic_short);
    profile.acoustic_ofdm=false;
    profile.ofdm_training_blocks=16;
    profile.ldpc_frame_bits=1944;
    profile.constellation=4;
    profile.carrier_hz=1800.;
    profile.symbol_rate=461.;
    auto waveform=render(profile,1,477);
    const auto sps=profile.sample_rate/profile.symbol_rate;
    // A noisy startup exercises the coherent branch without relying on
    // sparse model admission. Leave the independent marker and payload
    // clean so their result diagnoses only selection timing.
    const auto noisy_samples=static_cast<std::size_t>((fast::preamble_symbols(profile)+
        single_carrier_pulse_radius(profile)-16.)*sps);
    constexpr double symbol_snr=7.;
    const auto noise_sigma=profile.amplitude*std::sqrt(sps/std::pow(10.,symbol_snr/10.))*.5;
    std::mt19937 noise_random(3149);
    std::normal_distribution<float> noise(0.F,static_cast<float>(noise_sigma));
    for(auto& sample:std::span(waveform.pcm).first(noisy_samples))sample+=noise(noise_random);
    // The matched receive filter and last pilot need real samples beyond
    // the finite transmit pulse tail before emitting the interval.
    waveform.pcm.resize(waveform.pcm.size()+profile.sample_rate/4,0.F);
    const auto pcm=std::span(waveform.pcm);
    std::vector<Soft> reference_intervals;
    fast::Receiver coherent(profile,[&](auto soft) {
        reference_intervals.emplace_back();
        std::ranges::copy(soft,reference_intervals.back().begin());
    });
    std::size_t lock=0;
    while(lock<pcm.size()&&!coherent.progress().acquired)coherent.push(pcm.subspan(lock++,1));
    check(coherent.progress().acquired,"Narrow grace fixture could not acquire the coherent marker");
    const auto switch_at=lock+static_cast<std::size_t>(std::ceil(4.*fast::sync_symbols*sps));
    check(switch_at<pcm.size(),"Narrow grace boundary lies outside the fixture");
    coherent.push(pcm.subspan(lock));
    check(reference_intervals.size()==1,"Narrow grace reference did not emit one interval");

    for(const std::size_t chunk:{1U,4093U}) {
        std::size_t intervals=0;
        fast::Receiver competing(profile,[&](auto soft,bool) -> std::optional<fast::ValidatedCapacityCycle> {
            check(intervals<reference_intervals.size(),"Competing acquisition emitted an extra interval");
            check(std::ranges::equal(soft,reference_intervals[intervals]),
                  "Selected coherent receiver changed the acquired interval");
            ++intervals;
            return {};
        },{},{},{});
        for(std::size_t at=0;at<switch_at-1;) {
            const auto n=std::min(chunk,switch_at-1-at);
            competing.push(pcm.subspan(at,n));
            at+=n;
        }
        check(competing.backend_name()=="single-carrier-acquisition"&&!intervals,
              "Coherent acquisition ended its grace early or emitted before selection");
        competing.push(pcm.subspan(switch_at-1,1));
        check(competing.backend_name()=="standard-fast"&&competing.progress().acquired&&!intervals,
              "Coherent acquisition grace did not expire on the raw capture clock");
        competing.push(pcm.subspan(switch_at));
        check(intervals==1,"Competing acquisition dropped the selected coherent interval");
    }
}

fast::Profile dense_acoustic_profile() {
    auto profile=fast::capacity_profile(fast::Channel::acoustic_short);
    profile.acoustic_ofdm=false;
    profile.ofdm_training_blocks=16;
    profile.carrier_hz=1800.;
    profile.symbol_rate=1000.;
    profile.rolloff=.20;
    profile.amplitude*=std::sqrt(2.)/4.5;
    profile.constellation=4;
    profile.code_rate=fast::CodeRate::half;
    profile.ldpc_frame_bits=1944;
    profile.interleave_depth=1;
    profile.marker_spacing_intervals=1;
    profile.pilot_spacing_symbols=4;
    check(fast::dense_acoustic_framing(profile),"Dense Fast contract requires its fixed wire profile");
    return profile;
}

void check_dense_marker_clock() {
    const auto p=dense_acoustic_profile();
    constexpr std::uint64_t late_ordinal=100000;
    const DenseTiming startup{12100.,48.,0.};
    const auto elapsed=double(fast::preamble_symbols(p))+fast::total_interval_symbols(p,late_ordinal)+
        single_carrier_pulse_radius(p);
    const auto old_projection=startup.epoch+elapsed*startup.samples_per_symbol;
    const DenseTiming previous{old_projection+2.*p.sample_rate-elapsed*48.0048,48.0048,12.};
    const DenseTiming updated{old_projection+2.*p.sample_rate+6.-elapsed*48.0052,48.0052,12.};
    const auto cap=.024*p.sample_rate;
    check(!dense_marker_timing_matches(p,late_ordinal,startup,updated,cap),
        "Late Fast marker fixture did not leave the immutable startup tolerance");
    check(dense_marker_timing_matches(p,late_ordinal,previous,updated,cap),
        "Fast dense recurring timing rejected a local six-tick clock correction");
    const auto expected=old_projection+2.*p.sample_rate+6.+fast::interval_symbols(p)*updated.samples_per_symbol;
    check(dense_public_marker_tick(p,late_ordinal+1,updated)==static_cast<std::uint64_t>(std::ceil(expected)),
        "Fast dense recurring timing changed fixed public ordinal geometry");
    auto wrong=updated;
    wrong.epoch+=fast::interval_symbols(p)*updated.samples_per_symbol;
    check(!dense_marker_timing_matches(p,late_ordinal,previous,wrong,cap),
        "Fast dense recurring alignment accepted the next wire ordinal at the current slot");
}

void check_dense_acoustic_carrier_edges() {
    const auto profile=dense_acoustic_profile();
    constexpr std::uint64_t quota=1<<20;
    const datapump::Bytes source{'f','o','x'};
    const fast::ChannelAssumptions assumptions{};
    const auto admitted=fast::receiver_assumption_bounds(profile,assumptions).admitted.frequency_hz;
    for(const auto [seed,shift_hz,sign]:std::array{
            std::tuple{420U,112000000.,-1.},std::tuple{419U,332000000.,1.}}) {
        auto encoded=fast::prepare_xz_source(fast::byte_source(source),quota);
        auto encoder=fast::testing::deterministic_encoder(profile,{},fast::byte_source(std::move(encoded.encoded)),
            seed^0x4267e23dba723f99ULL,fast::SourceEncoding::xz);
        fast::Transmitter tx(profile,[&](auto bits){return encoder.next_interval(bits);});
        fast::SimulationSettings settings;
        settings.seed=seed;
        settings.shift_ppm=1.;
        settings.shift_phase_rad_sqrt_s=std::numbers::pi/3600.;
        settings.shift_hz=shift_hz;
        fast::SimulationChannel channel(profile,settings,[&](auto pcm){return tx.read_analytic(pcm);});
        const auto offset=channel.report().shift_error_hz;
        check(sign*offset>.99*admitted&&sign*offset<=admitted,
            "Dense Fast carrier-edge fixture is outside its declared edge region");
        fast::StreamDecoder decoder(profile,{},quota,fast::SourceEncoding::xz);
        fast::Receiver receiver(profile,[&](auto llrs,bool feedback)->std::optional<fast::ValidatedCapacityCycle> {
            check(!feedback,"Dense Fast carrier search consulted source validation");
            decoder.push_interval(llrs);
            return {};
        },{},{},{},assumptions);
        std::array<float,4096> samples{};
        while(!receiver.progress().physical_complete) {
            const auto count=channel.read(samples);
            if(!count)break;
            const auto consumed=receiver.consume(std::span(samples).first(count));
            check(consumed==count||receiver.progress().physical_complete,
                "Dense Fast carrier-edge receiver abandoned observed audio");
        }
        receiver.finish();
        decoder.finish(receiver.progress().physical_complete);
        const auto end=receiver.physical_completion_input_tick();
        const auto tail=channel.report().quiet_start_sample;
        std::cout<<"dense_carrier_edge_hz="<<offset<<" frames="<<decoder.snapshot().ldpc_frames
                 <<" tail="<<tail.value_or(0)<<" completion="<<end.value_or(0)<<'\n';
        check(tail&&end&&*end>=*tail+6ULL*profile.sample_rate&&decoder.result()&&
              decoder.snapshot().ldpc_frames==2&&std::ranges::equal(decoder.result()->bytes(),source),
              "Dense Fast carrier-edge receiver lost exact bytes or true physical completion");
    }
}

void check_dense_acoustic_contract() {
    check_dense_marker_clock();
    const auto profile=dense_acoustic_profile();

    constexpr std::uint64_t quota=1<<20;
    const datapump::Bytes source{'f','o','x'};
    auto encoded=fast::prepare_xz_source(fast::byte_source(source),quota);
    auto encoder=fast::testing::deterministic_encoder(profile,{},fast::byte_source(std::move(encoded.encoded)),
        421,fast::SourceEncoding::xz);
    fast::Transmitter tx(profile,[&](auto bits){return encoder.next_interval(bits);});
    fast::SimulationSettings settings;
    settings.seed=421;
    settings.delay_ms=7.;
    settings.doppler_hz=30.;
    settings.spread_evolution=fast::SpreadEvolution::gaussian;
    settings.transmit_watts=40.;
    settings.path_loss_db=130.;
    settings.noise_dbm_hz=-174.;
    fast::SimulationChannel channel(profile,settings,[&](auto pcm){return tx.read_analytic(pcm);});
    const auto assumptions=fast::channel_assumptions(settings);
    std::vector<float> samples;
    std::array<float,2053> chunk{};
    while(const auto n=channel.read(chunk))samples.insert(samples.end(),chunk.begin(),chunk.begin()+n);
    check(channel.report().quiet_start_sample.has_value(),"Dense Fast fixture did not produce a real quiet tail");
    const auto tail_start=*channel.report().quiet_start_sample;
    for(const auto gain:{1.F,.1F,.01F}) {
        fast::StreamDecoder decoder(profile,{},quota,fast::SourceEncoding::xz);
        fast::Receiver receiver(profile,[&](auto llrs,bool)->std::optional<fast::ValidatedCapacityCycle> {
            decoder.push_interval(llrs);
            return {};
        },{},{},{},assumptions);
        for(std::size_t offset=0;offset<samples.size()&&!receiver.progress().physical_complete;) {
            const auto n=std::min(chunk.size(),samples.size()-offset);
            for(std::size_t i=0;i<n;++i)chunk[i]=gain*samples[offset+i];
            const auto used=receiver.consume(std::span(chunk).first(n));
            check(used==n||receiver.progress().physical_complete,"Dense Fast abandoned gain-scaled PCM");
            offset+=used;
            check(!decoder.result(),"Dense Fast exposed source before delivery of physical completion");
        }
        receiver.finish();
        const auto end=receiver.physical_completion_input_tick();
        check(receiver.progress().acquired && end && *end>=tail_start+6ULL*profile.sample_rate,
            "Dense Fast gain scaling changed acquisition or scored physical absence");
        decoder.finish(receiver.progress().physical_complete);
        check(decoder.result() && std::ranges::equal(decoder.result()->bytes(),source),
            "Dense Fast inverse was not invariant to capture gain at equal SNR");
        check(receiver.workspace_bytes()<16U*1024*1024,"Dense Fast DSP exceeded its local tile workspace");
    }

    fast::StreamDecoder partial(profile,{},quota,fast::SourceEncoding::xz);
    fast::Receiver truncated(profile,[&](auto llrs,bool)->std::optional<fast::ValidatedCapacityCycle> {
        partial.push_interval(llrs);
        return {};
    },{},{},{},assumptions);
    const auto prefix=std::span(samples).first(tail_start+profile.sample_rate*11/2);
    check(truncated.consume(prefix)==prefix.size()&&truncated.progress().acquired,
        "Dense Fast truncated control did not exercise acquired reception");
    truncated.finish();
    partial.finish(truncated.progress().physical_complete);
    check(!truncated.physical_completion_input_tick()&&!partial.result(),
        "Dense Fast EOF or less than six seconds of silence completed a source");

    std::mt19937 random(421);
    std::normal_distribution<float> noise(0.F,.10F);
    fast::Receiver absent(profile,[](auto,bool)->std::optional<fast::ValidatedCapacityCycle> {
        throw std::runtime_error("Dense Fast white noise committed a payload interval");
    },{},{},{},assumptions);
    double maximum_burst=0.,backlog=0.,maximum_backlog=0.;
    for(std::size_t left=12*profile.sample_rate;left;) {
        const auto count=std::min(left,chunk.size());
        for(auto& sample:std::span(chunk).first(count))sample=noise(random);
        const auto start=std::chrono::steady_clock::now();
        check(absent.consume(std::span(chunk).first(count))==count,"Dense Fast ended on unacquired white noise");
        const auto service=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        maximum_burst=std::max(maximum_burst,service);
        backlog=std::max(0.,backlog+service-double(count)/profile.sample_rate);
        maximum_backlog=std::max(maximum_backlog,backlog);
        left-=count;
    }
    absent.finish();
    std::cout<<"dense_noise_only_seconds=12 workspace="<<absent.workspace_bytes()
             <<" max_push_seconds="<<maximum_burst<<" max_backlog_seconds="<<maximum_backlog<<'\n';
    check(!absent.progress().acquired&&!absent.physical_completion_input_tick(),
        "Dense Fast admitted noise without independently verified public alignment");
    check(absent.workspace_bytes()<16U*1024*1024,"Dense Fast rolling acquisition exceeded its fixed workspace");

    auto waveform=render(profile,3,338);
    const auto boundary=fast::preamble_symbols(profile)+fast::total_interval_symbols(profile,1);
    const auto radius=single_carrier_pulse_radius(profile);
    // Remove the whole physical aperture for the missing interval, including
    // its right RRC support. The following alignment word resumes after its
    // first 64 symbols, while its payload and later pilot words remain intact.
    const auto first=static_cast<std::size_t>((boundary+radius)*48.);
    const auto past=static_cast<std::size_t>((boundary+fast::interval_symbols(profile)+2*radius)*48.);
    std::ranges::fill(std::span(waveform.pcm).subspan(first,past-first),0.F);
    std::vector<Soft> received;
    fast::Receiver resumed(profile,[&](auto llrs,bool)->std::optional<fast::ValidatedCapacityCycle> {
        std::ranges::copy(llrs,received.emplace_back().begin());
        return {};
    },{},{},{},assumptions);
    check(resumed.consume(waveform.pcm)==waveform.pcm.size()&&!resumed.progress().physical_complete,
        "Dense Fast completed before a resumed public interval");
    std::vector<float> quiet(fast::end_silence_samples(profile));
    (void)resumed.consume(quiet);
    const auto end=resumed.physical_completion_input_tick();
    resumed.finish();
    std::cout<<"dense_resume_intervals="<<received.size()<<" erasures="<<resumed.progress().erased_intervals
             <<" tail_start="<<waveform.pcm.size()<<" completion="<<end.value_or(0)<<'\n';
    check(end&&*end>=waveform.pcm.size()+6ULL*profile.sample_rate&&received.size()==3&&
          resumed.progress().erased_intervals==1&&std::ranges::all_of(received[1],[](float value){return value==0.F;}),
          "Dense Fast lost a timed interior erasure or the subsequent real physical end");
    for(std::size_t i=0;i<waveform.bits.back().size();++i)
        check(received.back()[i]!=0.F && (received.back()[i]>0.F)==bool(waveform.bits.back()[i]),
            "Dense Fast did not recover the resumed payload at its original wire ordinal");
}

void check_dense_acoustic_transitions() {
    const auto profile=dense_acoustic_profile();
    constexpr std::uint64_t quota=1<<20;
    const auto key=fast_test::sequence_key();
    const datapump::Bytes source{'f','o','x'};
    auto encoded=fast::prepare_xz_source(fast::byte_source(source),quota);
    auto encoder=fast::testing::deterministic_encoder(profile,key,fast::byte_source(std::move(encoded.encoded)),
        419,fast::SourceEncoding::xz);
    fast::Transmitter tx(profile,[&](auto bits){return encoder.next_interval(bits);});
    const auto start_of_switch=static_cast<std::uint64_t>(profile.sample_rate/profile.symbol_rate*
        (fast::preamble_symbols(profile)+fast::interval_symbols(profile)/2));
    const auto end_of_switch=start_of_switch+profile.sample_rate/10;
    const auto input_samples=fast::transmission_samples(profile,2);
    std::vector<std::complex<float>> analytic(static_cast<std::size_t>(input_samples));
    check(tx.read_analytic(analytic)==analytic.size(),"Dense transition fixture did not render two native intervals");
    fast::SimulationSettings settings;
    settings.seed=419;
    settings.delay_ms=7.;
    settings.doppler_hz=30.;
    settings.spread_evolution=fast::SpreadEvolution::gaussian;
    settings.transmit_watts=40.;
    settings.path_loss_db=130.;
    settings.noise_dbm_hz.reset();
    const auto assumptions=fast::channel_assumptions(settings);
    std::size_t read=0;
    fast::SimulationChannel channel(profile,settings,[&](auto pcm) {
        const auto n=std::min(analytic.size()-read,pcm.size());
        std::ranges::copy(std::span(analytic).subspan(read,n),pcm.begin());
        read+=n;
        return n;
    });
    std::vector<float> faded;
    std::array<float,2053> chunk{};
    while(const auto n=channel.read(chunk))faded.insert(faded.end(),chunk.begin(),chunk.begin()+n);
    const auto tail_start=channel.report().quiet_start_sample;
    check(tail_start.has_value(),"Dense transition fixture lacked a real propagation drain");
    for(bool initially_faded:{false,true}) {
        fast::StreamDecoder decoder(profile,key,quota,fast::SourceEncoding::xz);
        std::size_t display_points=0;
        bool finite_display=true;
        fast::Receiver receiver(profile,[&](auto llrs,bool feedback)->std::optional<fast::ValidatedCapacityCycle> {
            check(!feedback,"Dense model arbitration consulted the decoder for candidate selection");
            decoder.push_interval(llrs);
            return {};
        },[&](auto point) {
            finite_display&=std::isfinite(point.real())&&std::isfinite(point.imag());
            ++display_points;
        },{},{},assumptions);
        std::size_t offset=0;
        while(offset<faded.size()&&!receiver.progress().physical_complete) {
            const auto n=std::min(chunk.size(),faded.size()-offset);
            for(std::size_t i=0;i<n;++i) {
                const auto tick=offset+i;
                const auto fraction=std::clamp((double(tick)-start_of_switch)/(end_of_switch-start_of_switch),0.,1.);
                const auto weight=initially_faded?1.-fraction:fraction;
                const auto clean=tick<analytic.size()?analytic[tick].real():0.F;
                chunk[i]=static_cast<float>((1.-weight)*clean+weight*faded[tick]);
            }
            const auto used=receiver.consume(std::span(chunk).first(n));
            check(used==n||receiver.progress().physical_complete,"Dense transition receiver lost input support");
            offset+=used;
        }
        const auto end=receiver.physical_completion_input_tick();
        receiver.finish();
        decoder.finish(receiver.progress().physical_complete);
        std::cout<<"dense_transition_initially_faded="<<initially_faded
                 <<" intervals="<<receiver.progress().intervals<<" frames="<<decoder.snapshot().ldpc_frames
                 <<" failed="<<decoder.snapshot().ldpc_failed_frames<<" completion="<<end.value_or(0)<<'\n';
        check(end&&*end>=*tail_start+6ULL*profile.sample_rate&&decoder.result()&&decoder.snapshot().authenticated&&
              std::ranges::equal(decoder.result()->bytes(),source)&&decoder.snapshot().intervals==2,
              "Dense model arbitration lost exact keyed bytes across a channel change within one interval");
        check(finite_display&&display_points>1024&&display_points<=2*dense_points_per_interval&&
              receiver.progress().last_present_signal&&receiver.progress().last_present_signal->past_last<*tail_start,
              "Dense model arbitration displayed missing or non-payload constellation samples");
    }
}

void check_dense_marker_reentry() {
    const auto p=dense_acoustic_profile();
    const auto waveform=render(p,2,419);
    std::size_t intervals=0;
    fast::Transmitter tx(p,[&](auto bits) {
        if(intervals>=waveform.bits.size())return false;
        std::ranges::copy(waveform.bits[intervals++],bits.begin());
        return true;
    });
    fast::SimulationSettings s;
    s.seed=419;
    s.spread_evolution=fast::SpreadEvolution::gaussian;
    s.delay_ms=7.;
    s.doppler_hz=30.;
    s.noise_dbm_hz.reset();
    fast::SimulationChannel ch(p,s,[&](auto pcm){return tx.read_analytic(pcm);});
    const auto first=static_cast<std::uint64_t>(p.sample_rate/p.symbol_rate*
        (fast::preamble_symbols(p)+fast::interval_symbols(p)));
    constexpr std::size_t hint_tick=144000;
    std::size_t emitted=0,known=0,errors=0;
    bool timing_verified=false;
    DenseReceiverObservers observers;
    observers.interval=[&](const DenseIntervalReceipt& receipt) {
        if(receipt.ordinal!=1)return;
        const auto usable=std::ranges::any_of(receipt.llrs,[](float bit){return bit!=0.F;});
        check(timing_verified||!usable,"Adopted dense model emitted usable data before marker authorization");
        ++emitted;
        for(std::size_t i=0;i<receipt.llrs.size();++i)if(receipt.llrs[i]!=0.F) {
            ++known;
            errors+=(receipt.llrs[i]>0.F)!=bool(waveform.bits[1][i]);
        }
    };
    observers.timing=[&](std::uint64_t ordinal,DenseTiming) {
        check(ordinal==1,"Adopted dense model fabricated an ordinal-zero marker");
        timing_verified=true;
    };
    DenseAcousticReceiver receiver(p,{}, {},{}, {},fast::ChannelBounds{300.,100.,22.},std::move(observers));
    std::size_t consumed=0;
    std::array<float,2053> block{};
    while(const auto n=ch.read(std::span(block).first(consumed<hint_tick?
        std::min(hint_tick-consumed,block.size()):block.size()))) {
        if(consumed<hint_tick&&consumed+n==hint_tick) {
            std::ranges::fill(std::span(block).first(n),0.F);
            check(receiver.consume(std::span(block).first(n))==n&&!receiver.progress().acquired,
                "Reentry fixture unexpectedly acquired before a borrowed marker");
            consumed+=n;
            check(receiver.adopt_timing({0.,48.,0.},1,true),"Dense model rejected the bounded future public marker hint");
            continue;
        }
        for(std::size_t i=0;i<n;++i)if(consumed+i+64<first)block[i]=0.F;
        const auto used=receiver.consume(std::span(block).first(n));
        consumed+=used;
        if(emitted)break;
        check(used==n,"Dense marker reentry stopped before an observed resumed interval");
    }
    receiver.finish();
    std::cout<<"dense_reentry_authorized="<<timing_verified<<" known="<<known<<" errors="<<errors<<'\n';
    check(timing_verified&&emitted==1&&known>1900&&errors<70,
        "Dense model did not independently resolve the adopted marker's delayed path and resume evidence");

    DenseReceiverObservers control;
    control.interval=[](const DenseIntervalReceipt& receipt) {
        check(std::ranges::all_of(receipt.llrs,[](float bit){return bit==0.F;}),
            "A borrowed public timetable authorized payload from white noise");
    };
    control.timing=[](std::uint64_t,DenseTiming) {
        throw std::runtime_error("A borrowed public timetable authorized a noise-only marker");
    };
    DenseAcousticReceiver absent(p,{}, {},{}, {},fast::ChannelBounds{300.,100.,22.},std::move(control));
    std::mt19937 rng(418);
    std::normal_distribution<float> gaussian(0.F,static_cast<float>(p.amplitude/std::sqrt(2.)));
    constexpr auto preroll=2*48000;
    std::size_t offset=0;
    const auto end=first+static_cast<std::uint64_t>((fast::interval_symbols(p)/p.symbol_rate+.2)*p.sample_rate);
    while(offset<end) {
        const auto stop=offset<preroll?std::uint64_t(preroll):end;
        const auto n=std::min<std::uint64_t>(block.size(),stop-offset);
        for(auto& sample:std::span(block).first(n))sample=gaussian(rng);
        check(absent.consume(std::span(block).first(n))==n,"Borrowed marker noise control lost actual support");
        offset+=n;
        if(offset==preroll)
            check(absent.adopt_timing({0.,48.,0.},1,true),"Noise-only control did not schedule the borrowed marker");
    }
    absent.finish();
    check(!absent.progress().acquired&&!absent.completion_tick(),
        "Borrowed cadence or EOF completed the noise-only marker control");
}

void check_dense_recurring_epoch() {
    const auto p=dense_acoustic_profile();
    constexpr std::size_t recovery_ordinal=5;
    auto original=render(p,recovery_ordinal+2,629);
    constexpr double relative_source_clock=-55e-6;
    // Preserve the transmitted payloads while four recurring markers fade.
    // Later evidence must recover its actual ordinal after the search extent
    // has saturated, rather than depend on an earlier marker reanchor.
    const auto pulse_radius=single_carrier_pulse_radius(p);
    for(std::size_t ordinal=1;ordinal<recovery_ordinal;++ordinal) {
        const auto marker_start=fast::preamble_symbols(p)+fast::total_interval_symbols(p,ordinal);
        const auto first=static_cast<std::size_t>((marker_start+pulse_radius-2.)*48.);
        const auto past=static_cast<std::size_t>((marker_start+pulse_radius+190.)*48.);
        std::ranges::fill(std::span(original.pcm).subspan(first,past-first),0.F);
    }
    datapump::audio::Resampler converter(p.sample_rate,p.sample_rate,true);
    // Source time is exact at startup and starts drifting after the first
    // independently verified public marker. The accumulated offset exceeds
    // the former fixed local cap by the time an informative marker resumes.
    const auto change=(fast::preamble_symbols(p)+260)*48;
    std::vector<float> input;
    std::array<float,2053> block{};
    std::size_t consumed=0;
    while(!converter.finished()) {
        if(consumed==change)converter.set_rate_correction(relative_source_clock,.001);
        const auto endpoint=consumed<change?change:original.pcm.size();
        const auto progress=converter.process(std::span(original.pcm).subspan(consumed,endpoint-consumed),
            block,endpoint==original.pcm.size());
        check(progress.consumed||progress.produced||converter.finished(),"Dense clock fixture resampler stalled");
        consumed+=progress.consumed;
        input.insert(input.end(),block.begin(),block.begin()+progress.produced);
    }
    const auto tail_start=input.size();
    input.resize(tail_start+fast::end_silence_samples(p));
    std::vector<std::pair<std::uint64_t,DenseTiming>> markers;
    std::array<std::size_t,recovery_ordinal+2> observed{},errors{};
    DenseReceiverObservers observations;
    observations.timing=[&](std::uint64_t ordinal,DenseTiming timing){markers.emplace_back(ordinal,timing);};
    observations.interval=[&](const DenseIntervalReceipt& receipt) {
        if(receipt.ordinal>=observed.size())return;
        for(std::size_t i=0;i<receipt.llrs.size();++i)if(receipt.llrs[i]!=0.F) {
            ++observed[receipt.ordinal];
            errors[receipt.ordinal]+=(receipt.llrs[i]>0.F)!=bool(original.bits[receipt.ordinal][i]);
        }
    };
    DenseAcousticReceiver receiver(p,{}, {},{}, {},fast::ChannelBounds{300.,100.,22.},std::move(observations));
    std::size_t at=0;
    while(at<input.size()&&!receiver.progress().physical_complete) {
        const auto count=std::min(block.size(),input.size()-at);
        const auto used=receiver.consume(std::span(input).subspan(at,count));
        check(used==count||receiver.progress().physical_complete,"Dense recurring marker dropped actual input");
        at+=used;
    }
    receiver.finish();
    const auto end=receiver.completion_tick();
    std::cout<<"dense_recurring_timing";
    for(const auto& [ordinal,timing]:markers)std::cout<<" ordinal="<<ordinal<<" epoch="<<timing.epoch;
    std::cout<<" observed="<<observed[recovery_ordinal]<<'/'<<observed[recovery_ordinal+1]
             <<" errors="<<errors[recovery_ordinal]<<'/'<<errors[recovery_ordinal+1]
             <<" tail="<<tail_start<<" completion="<<end.value_or(0)<<'\n';
    check(markers.size()>=3&&markers.front().first==0&&markers[1].first==recovery_ordinal&&
        markers[2].first==recovery_ordinal+1&&
        std::abs(markers[1].second.epoch-markers.front().second.epoch)>18.&&
        markers[1].second.samples_per_symbol==markers.front().second.samples_per_symbol,
        "Dense later marker did not verify a same-ordinal epoch correction after a missing marker");
    check(observed[recovery_ordinal]>1900&&observed[recovery_ordinal+1]>1900&&
        errors[recovery_ordinal]<70&&errors[recovery_ordinal+1]<70,
        "Dense recurring epoch change did not preserve the later payload positions");
    check(end&&*end>=tail_start+6ULL*p.sample_rate,"Dense recurring epoch change did not score a real six-second quiet tail");
}

void check_noisy_tail(bool resume,bool short_echo=false) {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    check(profile.constellation==4 && profile.code_rate==fast::CodeRate::half &&
          profile.pilot_spacing_symbols==16 && fast::ssb_capacity_framing(profile),
          "Noisy-tail fixture requires the canonical dense-pilot SSB format");
    const std::size_t count=resume?9:1;
    auto [source,waveform]=render(profile,count,4109);
    if(short_echo)
        // A one-symbol physical echo exercises the selected local multipath
        // model. Neither the delay nor the noise level is given to reception.
        waveform=one_symbol_echo(waveform,profile);
    const auto tail_samples=fast::end_silence_samples(profile);
    std::vector<float> tail(static_cast<std::size_t>(tail_samples));
    const auto short_noise_samples=static_cast<std::size_t>(profile.sample_rate*11/2);
    double peak=0.;
    if(resume) {
        const auto boundary=fast::preamble_symbols(profile)+fast::total_interval_symbols(profile,1)+
            std::ceil(6.4/profile.rolloff)+24;
        const auto gap_start=static_cast<std::size_t>(boundary*profile.sample_rate/profile.symbol_rate);
        const auto gap=std::span(waveform).subspan(gap_start,short_noise_samples);
        peak=short_echo?low_level_noise(gap,547):colored_interference(gap,profile,547);
    }
    if(short_echo)peak=std::max(peak,low_level_noise(tail,613));
    else if(!resume)peak=colored_interference(tail,profile,547);
    check(peak<.999,"Noisy-tail fixture exceeded the PCM range");

    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    std::vector<Soft> received;
    SsbReceiver receiver(profile,geometry,bounds,.10,
        [&](std::span<const float> interval,bool feedback) -> std::optional<fast::ValidatedCapacityCycle> {
            check(!feedback&&received.size()<count,"Noise became a phantom source interval");
            std::ranges::copy(interval,received.emplace_back().begin());
            return {};
        },{},short_echo?SsbReceiverModelPolicy::validate_local_fir:SsbReceiverModelPolicy::sparse_delay_doppler);
    const auto feed=[&](std::span<const float> pcm,bool allow_end) {
        constexpr std::size_t chunk=1023;
        for(std::size_t offset=0;offset<pcm.size() && !receiver.progress().physical_complete;offset+=chunk) {
            const auto part=pcm.subspan(offset,std::min(chunk,pcm.size()-offset));
            check(receiver.push(part)==part.size(),"Noisy-tail receiver lost an input sample");
        }
        if(!allow_end)check(!receiver.progress().physical_complete,
            "Less than six observed seconds ended a continuing or incomplete stream");
    };
    const auto crop=profile.sample_rate/50;
    feed(std::span(waveform).subspan(crop),false);
    check(receiver.progress().acquired,"No-idle noisy-tail fixture failed acquisition");
    feed(std::span(tail).first(short_noise_samples),false);
    feed(std::span(tail).subspan(short_noise_samples),true);
    const auto completion=receiver.completion_tick();
    const auto tail_origin=waveform.size()-crop;
    std::cout<<"noisy_resume="<<resume<<" short_echo="<<short_echo<<" clipped_start="<<crop<<" interference_peak="<<peak
             <<" intervals="<<received.size()<<" erasures="<<receiver.progress().erased_intervals
             <<" tail_origin="<<tail_origin<<" completion_tick="<<completion.value_or(0)<<std::endl;
    if(short_echo) {
        const auto& model=receiver.model_progress();
        std::cout<<"local_fir="<<(model.selected==SsbReceiverModel::local_fir)
                 <<" eligible="<<model.local_eligible<<" tested="<<model.local_tested
                 <<" g3="<<model.local_admission_nmse.value_or(-1.)
                 <<" g7="<<model.local_prospective_nmse.value_or(-1.)<<'\n';
    }
    print_quiet(receiver);
    receiver.finish();
    check(completion && *completion>=tail_origin+6ULL*profile.sample_rate,
          "Noisy-tail receiver lacked six seconds of physical quiet after the transmitted waveform");
    if(short_echo)check(receiver.model_progress().selected==SsbReceiverModel::local_fir &&
          receiver.quiet_progress().low_power_calibrated_units,
          "Short-echo tail did not exercise the independently calibrated local-FIR quiet route");
    check(received.size()==count,"Noisy-tail receiver omitted an interval or committed trailing noise");
    if(resume)check(receiver.progress().erased_intervals>=1,"The 5.5-second disturbance lost its timed holes");
    for(std::size_t i=0;i<source.back().size();++i)
        check(received.back()[i]!=0.F && (received.back()[i]>0.F)==bool(source.back()[i]),
              "Noisy-tail fixture failed its last actual payload interval");
}

void check_weak_terminal_interval() {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    auto [source,transmitted]=render(profile,2,4109);
    const auto boundary=fast::preamble_symbols(profile)+fast::total_interval_symbols(profile,1);
    const auto radius=std::ceil(6.4/profile.rolloff);
    const auto center=boundary+radius-24.;
    const auto fade=static_cast<std::size_t>(center*profile.sample_rate/profile.symbol_rate);
    const auto final_symbol=fast::preamble_symbols(profile)+fast::total_interval_symbols(profile,2);
    const auto fragment_center=final_symbol-3*(profile.pilot_spacing_symbols+fast::capacity_pilot_symbols)+radius;
    const auto fragment=static_cast<std::size_t>(fragment_center*profile.sample_rate/profile.symbol_rate);
    std::ranges::fill(std::span(transmitted).subspan(fade,fragment-fade),0.F);
    for(auto& sample:std::span(transmitted).subspan(fragment))sample*=.01F;
    const auto waveform=one_symbol_echo(transmitted,profile);

    std::vector<Soft> received;
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    SsbReceiver receiver(profile,make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz),bounds,.10,
        [&](std::span<const float> soft,bool feedback)->std::optional<fast::ValidatedCapacityCycle> {
            check(!feedback&&received.size()<source.size(),"Weak terminal control emitted a phantom interval");
            std::ranges::copy(soft,received.emplace_back().begin());
            return {};
        },{},SsbReceiverModelPolicy::validate_local_fir);
    constexpr std::size_t chunk=1023;
    for(std::size_t offset=0;offset<waveform.size();offset+=chunk) {
        const auto input=std::span(waveform).subspan(offset,std::min(chunk,waveform.size()-offset));
        check(receiver.push(input)==input.size() && !receiver.progress().physical_complete,
            "A weak continuing public frame caused early physical end");
    }
    std::array<float,chunk> quiet{};
    for(auto left=fast::end_silence_samples(profile);left && !receiver.progress().physical_complete;) {
        const auto count=std::min<std::uint64_t>(left,quiet.size());
        check(receiver.push(std::span(quiet).first(count))==count,"Weak terminal quiet control lost PCM");
        left-=count;
    }
    const auto completion=receiver.completion_tick();
    const auto& state=receiver.progress();
    const auto& quiet_state=receiver.quiet_progress();
    std::cout<<"weak_terminal_intervals="<<state.intervals<<" erasures="<<state.erased_intervals
             <<" low_power_vetoes="<<quiet_state.low_power_coherent_veto_units
             <<" true_tail_origin="<<waveform.size()<<" completion_tick="<<completion.value_or(0)<<std::endl;
    receiver.finish();
    check(receiver.model_progress().selected==SsbReceiverModel::local_fir && quiet_state.low_power_coherent_veto_units,
          "Weak terminal control did not exercise the local-FIR coherent veto");
    check(received.size()==2 && state.erased_intervals==1 &&
          std::ranges::all_of(received.back(),[](float soft){return soft==0.F;}),
          "Weak physical presence failed to commit its timed erasure");
    check(completion && *completion>=waveform.size()+6ULL*profile.sample_rate,
          "Weak terminal control lacked six physical quiet seconds");
}
}

int main(int argc,char** argv) {
    try {
        if(argc==1) {
            check_encrypted_stream();
            check_interior_hole();
            check_narrow_acoustic();
            check_narrow_acoustic_acquisition_grace();
        } else if(argc==2 && std::string_view(argv[1])=="--noise-tail")check_noisy_tail(false);
        else if(argc==2 && std::string_view(argv[1])=="--noise-resume")check_noisy_tail(true);
        else if(argc==2 && std::string_view(argv[1])=="--low-power-tail")check_noisy_tail(false,true);
        else if(argc==2 && std::string_view(argv[1])=="--low-power-resume")check_noisy_tail(true,true);
        else if(argc==2 && std::string_view(argv[1])=="--weak-terminal")check_weak_terminal_interval();
        else if(argc==2 && std::string_view(argv[1])=="--hf-quiet")check_hf_quiet();
        else if(argc==2 && std::string_view(argv[1])=="--dense-acoustic-contract")check_dense_acoustic_contract();
        else if(argc==2 && std::string_view(argv[1])=="--dense-carrier-edges")check_dense_acoustic_carrier_edges();
        else if(argc==2 && std::string_view(argv[1])=="--dense-acoustic-transitions")check_dense_acoustic_transitions();
        else if(argc==2 && std::string_view(argv[1])=="--dense-marker-clock")check_dense_marker_clock();
        else if(argc==2 && std::string_view(argv[1])=="--dense-marker-reentry")check_dense_marker_reentry();
        else if(argc==2 && std::string_view(argv[1])=="--dense-recurring-epoch")check_dense_recurring_epoch();
        else if(argc==2 && std::string_view(argv[1])=="--narrow-acoustic") {
            check_narrow_acoustic();
            check_narrow_acoustic_acquisition_grace();
        } else if(argc==2 && std::string_view(argv[1])=="--narrow-acoustic-grace")check_narrow_acoustic_acquisition_grace();
        else throw std::invalid_argument("Unknown Fast SSB receiver fixture");
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
