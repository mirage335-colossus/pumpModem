#include "fast/ssb_receiver.hpp"
#include "fast_test_key.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/simulation.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
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
    const auto fragment_center=final_symbol-3*(profile.pilot_spacing_symbols+fast::ssb_capacity_pilot_symbols)+radius;
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
        } else if(argc==2 && std::string_view(argv[1])=="--noise-tail")check_noisy_tail(false);
        else if(argc==2 && std::string_view(argv[1])=="--noise-resume")check_noisy_tail(true);
        else if(argc==2 && std::string_view(argv[1])=="--low-power-tail")check_noisy_tail(false,true);
        else if(argc==2 && std::string_view(argv[1])=="--low-power-resume")check_noisy_tail(true,true);
        else if(argc==2 && std::string_view(argv[1])=="--weak-terminal")check_weak_terminal_interval();
        else if(argc==2 && std::string_view(argv[1])=="--hf-quiet")check_hf_quiet();
        else throw std::invalid_argument("Unknown Fast SSB receiver fixture");
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
