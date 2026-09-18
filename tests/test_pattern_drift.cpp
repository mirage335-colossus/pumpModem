#include "datapump/pattern_code.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "../src/pattern_drift.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <tuple>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
constexpr std::size_t workspace=4*1024*1024;
constexpr std::size_t delay=37;
constexpr double tau=2*std::numbers::pi;
enum class Impairment {steady,phase_flip,wander,power_steps,wander_and_power,noise,carrier};
enum class Backend {fft,compact};

modem::Config config(bool keyed=false,unsigned chips=256) {
    modem::Config c;
    c.sample_rate=256;c.bandwidth_hz=32;c.carrier_hz=64;c.spreading_factor=chips;
    c.stream_epoch=1800000041;c.scramble=keyed;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(13*i+29);
    return c;
}

// Alter analytic I/Q before projecting to real PCM. Multiplying an already
// real waveform by cos(phase) would change amplitude rather than carrier phase.
// The receiver receives no transmitted bits, exact start, channel seed, phase
// trajectory or amplitude schedule through this helper.
std::vector<float> waveform(const modem::Config& c,const Bytes& bits,Impairment impairment,
                            std::uint64_t seed=7919,double noise_sigma=.25) {
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(tx.total_samples()));
    check(tx.read_analytic(analytic)==analytic.size(),"drift fixture did not generate its complete existing waveform");
    const auto padding=static_cast<std::size_t>(modem::pattern_pulse_padding_samples(c));
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    check(analytic.size()==bits.size()*symbol+2*padding,"drift fixture changed the exact raw-bit waveform endpoint");
    const auto payload=bits.size()*symbol;
    std::vector<float> pcm(delay+payload+2*symbol+c.sample_rate);
    std::mt19937_64 noise_random(seed),phase_random(seed^0x9677cb249aed438bULL);
    std::normal_distribution<double> noise(0,noise_sigma),walk(0,.65/std::sqrt(c.sample_rate));
    double phase=.73;
    for(std::size_t i=0;i<pcm.size();++i) {
        pcm[i]=static_cast<float>(noise(noise_random));
        if(i<delay||i>=delay+payload||impairment==Impairment::noise)continue;
        const auto at=i-delay,within=at%symbol;
        double gain=1,rotation=.73;
        if(impairment==Impairment::phase_flip && within>=symbol/2)rotation+=std::numbers::pi;
        if(impairment==Impairment::wander || impairment==Impairment::wander_and_power) {
            phase+=walk(phase_random);
            rotation=phase+2*std::sin(tau*static_cast<double>(at)/(1.31*symbol));
        }
        if(impairment==Impairment::power_steps || impairment==Impairment::wander_and_power) {
            // Deliberately offset the power changes from the receiver's four
            // section boundaries. A step changes received power only.
            constexpr std::array gains{1.2,.35,1.6,.7};
            gain=gains[((within+symbol/11)*4/symbol)%gains.size()];
            // The power-only control remains decodable by the historical
            // coherent detector; the combined-drift case keeps full steps.
            if(impairment==Impairment::power_steps)gain=1+.25*(gain-1);
        }
        const auto sample=impairment==Impairment::carrier?
            std::polar(std::sqrt(2*modem::nominal_signal_power),tau*c.carrier_hz*static_cast<double>(at)/c.sample_rate):
            analytic[padding+at];
        pcm[i]+=static_cast<float>((gain*std::polar(1.,rotation)*sample).real());
    }
    return pcm;
}

modem::PatternSearch search(bool drift) {
    modem::PatternSearch s;
    s.drift_tolerant=drift;s.frequency_offsets_hz={0};s.worker_threads=1;
    // The compact receiver gets an ordinary finite clock window, including
    // starts before and after the true start; there is no aligned-bit input.
    s.start_offset_seconds=.125;
    s.start_uncertainty_seconds=1./16;s.chunk_bits=1;
    s.candidate_limit=47;s.track_limit=4;s.bit_limit=64;
    return s;
}
struct Result {
    Bytes bits;
    std::vector<modem::PatternBurst> events;
    std::vector<modem::PatternEvidence> candidates;
    std::size_t bit_polls=0,completions=0,peak_working=0;
    double milliseconds=0;
};

template<class Receiver>
Result receive_with(Receiver& receiver,const modem::Config& c,const std::vector<float>& pcm,
                    std::size_t chunk,bool finish,std::size_t already_observed=0) {
    Result result;
    const auto begun=std::chrono::steady_clock::now();
    const auto drain=[&](std::size_t position) {
        result.peak_working=std::max(result.peak_working,receiver.working_bytes());
        check(result.peak_working<=workspace,"drift reception exceeded its declared DSP workspace");
        bool progressed=false;
        for(auto event:receiver.take_bursts()) {
            check(position>=modem::symbol_sample_count(c),"partial drift sections manufactured a bit before a whole symbol");
            if(event.complete) {
                check(position>=event.end_sample+modem::pattern_absence_samples(c),
                      "drift reception completed before observing the required whole absent symbol");
                ++result.completions;
            }
            progressed|=!event.bits.empty();
            result.bits.insert(result.bits.end(),event.bits.begin(),event.bits.end());
            result.bits.insert(result.bits.end(),event.missing_slots,modem::missing_pattern_bit);
            result.events.push_back(std::move(event));
        }
        if(progressed)++result.bit_polls;
    };
    for(std::size_t position=already_observed;position<pcm.size();) {
        const auto count=std::min(chunk,pcm.size()-position);
        receiver.push(std::span(pcm).subspan(position,count));position+=count;drain(position);
    }
    if(finish) {
        receiver.finish();drain(pcm.size());
        receiver.finish();check(receiver.take_bursts().empty(),"drift EOF emitted duplicate reception progress");
    }
    result.candidates=receiver.candidates();
    check(result.candidates.size()<=47,"drift candidate diagnostics exceeded their retained bound");
    result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begun).count();
    return result;
}
Result receive(Backend backend,const modem::Config& c,const std::vector<float>& pcm,
               bool drift=true,std::size_t chunk=257,bool finish=false,unsigned workers=1) {
    auto s=search(drift);s.worker_threads=workers;
    if(backend==Backend::compact) {
        modem::PatternCorrelator receiver(c,s,workspace);
        check(receiver.drift_tolerant()==(modem::detail::drift_section_count(c,drift)>1),
              "compact drift fixture silently disabled an eligible requested detector");
        return receive_with(receiver,c,pcm,chunk,finish);
    }
    modem::PatternReceiver receiver(c,workspace,s);
    check(!receiver.clock_windowed(),"FFT drift fixture silently used the compact fallback");
    check(receiver.drift_tolerant()==(modem::detail::drift_section_count(c,drift)>1),
          "FFT drift fixture silently disabled an eligible requested detector");
    return receive_with(receiver,c,pcm,chunk,finish);
}
void exact(const Result& result,const Bytes& expected,bool complete) {
    if(result.bits!=expected) {
        std::string observed;for(const auto bit:result.bits)observed+=static_cast<char>('0'+bit);
        throw Error("drift reception changed, duplicated or omitted an exact raw-bit prefix: "+observed);
    }
    check(result.bit_polls==expected.size(),"drift reception batched accepted bits behind a later symbol");
    check(result.completions==(complete?1U:0U),"drift reception produced the wrong physical completion state");
    std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
    std::size_t count=0;
    for(const auto& event:result.events) {
        check(!event.missing_slots,"drift fixture acquired an unexpected missing raw-bit slot");
        const auto current=std::pair{event.stream_first_sample,event.stream_first_symbol};
        if(!identity)identity=current;
        check(current==identity,"drift reception replaced one pending stream with another identity");
        count+=event.bits.size();
        check(count<=expected.size()&&std::equal(result.bits.begin(),result.bits.begin()+static_cast<std::ptrdiff_t>(count),expected.begin()),
              "an intermediate drift reception prefix differs from the entered bits");
    }
}

void drifting_symbols_and_power_steps() {
    const Bytes bits{0,0,1};
    for(const auto backend:{Backend::fft,Backend::compact})for(const bool keyed:{false,true}) {
        const auto c=config(keyed);
        for(const auto impairment:{Impairment::steady,Impairment::phase_flip,Impairment::wander,
                                   Impairment::power_steps,Impairment::wander_and_power}) {
            const auto pcm=waveform(c,bits,impairment);
            const auto enabled=receive(backend,c,pcm);
            try {exact(enabled,bits,true);}
            catch(const Error& error) {throw Error(std::string(backend==Backend::fft?"FFT":"Compact")+
                (keyed?" private":" public")+" impairment "+std::to_string(static_cast<unsigned>(impairment))+": "+error.what());}
            if(impairment==Impairment::phase_flip) {
                const auto coherent=receive(backend,c,pcm,false);
                check(coherent.bits!=bits,"phase-cancellation fixture must distinguish drift-tolerant reception from the coherent baseline");
            }
        }
    }
}

void partial_symbols_and_eof_are_not_absence() {
    const Bytes bits{0,0,1};
    for(const auto backend:{Backend::fft,Backend::compact})for(const bool keyed:{false,true}) {
        const auto c=config(keyed);
        auto pcm=waveform(c,bits,Impairment::wander_and_power);
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const std::vector<float> partial(pcm.begin(),pcm.begin()+static_cast<std::ptrdiff_t>(symbol-1));
        const auto unfinished=receive(backend,c,partial,true,37,true);
        check(unfinished.bits.empty()&&unfinished.completions==0,"EOF classified unfinished drift sections as a symbol");
        // Six seconds is shorter than this 16-second symbol. It cannot finish
        // reception until that absent symbol has been scored in full.
        pcm.resize(delay+bits.size()*symbol+6*c.sample_rate);
        exact(receive(backend,c,pcm,true,37,true),bits,false);
    }
}

void reject_unrelated_waveforms() {
    const Bytes bits{0,1,0};
    for(const auto backend:{Backend::fft,Backend::compact})for(const bool keyed:{false,true}) {
        const auto c=config(keyed);
        for(const auto seed:{101U,7919U,65537U}) {
            const auto noise=receive(backend,c,waveform(c,bits,Impairment::noise,seed));
            check(noise.bits.empty()&&noise.completions==0,"additional drift fits admitted seeded noise-only PCM");
        }
        if(keyed) {
            auto wrong=c;wrong.spreading_seed[0]^=1;
            const auto unrelated=waveform(c,bits,Impairment::wander_and_power);
            const auto baseline=receive(backend,wrong,unrelated,false);
            check(baseline.bits.empty(),"wrong-key control must be independently rejected by the historical coherent detector");
            const auto mismatched=receive(backend,wrong,unrelated);
            check(mismatched.bits.empty()&&mismatched.completions==0,"independent private patterns became interchangeable under drift fitting");
        }
        const auto unrelated=waveform(c,bits,Impairment::carrier);
        check(receive(backend,c,unrelated,false).bits.empty(),"carrier control must be rejected by the historical coherent detector");
        const auto carrier=receive(backend,c,unrelated);
        check(carrier.bits.empty()&&carrier.completions==0,"constant carrier interference was accepted as a drifting pattern");
    }
}

void drifting_signal_below_sample_noise() {
    const Bytes bits{0,0,1};
    auto c=config(true,1024);c.bandwidth_hz=128;
    for(const auto impairment:{Impairment::phase_flip,Impairment::wander}) {
        const auto pcm=waveform(c,bits,impairment,7919,1.);
        const auto noise=waveform(c,bits,Impairment::noise,7919,1.);
        const auto end=delay+bits.size()*modem::symbol_sample_count(c);
        double signal_energy=0,noise_energy=0;
        for(std::size_t i=delay;i<end;++i) {
            const auto signal=static_cast<double>(pcm[i])-noise[i];
            signal_energy+=signal*signal;noise_energy+=static_cast<double>(noise[i])*noise[i];
        }
        const auto measured_snr=10*std::log10(signal_energy/noise_energy);
        check(measured_snr< -3,"below-noise drift fixture must measure negative sample SNR with ample margin");
        for(const auto backend:{Backend::fft,Backend::compact}) {
            const auto baseline=receive(backend,c,pcm,false),drift=receive(backend,c,pcm,true);
            check(baseline.bits!=bits,"below-noise phase fixture must expose a limitation of the coherent baseline");
            exact(drift,bits,true);
        }
        std::cout<<"Below-noise "<<(impairment==Impairment::phase_flip?"phase flip":"phase wander")
                 <<": measured sample SNR "<<measured_snr<<" dB\n";
    }
}

void isolated_sections_are_not_whole_bits() {
    const auto c=config(true);const Bytes bit{1};
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto signal=waveform(c,bit,Impairment::steady,1237,1.);
    const auto noise=waveform(c,bit,Impairment::noise,1237,1.);
    for(const auto quarter:{0U,3U}) {
        auto fragment=noise;
        // Only one legal quarter survives; the rest of this complete symbol
        // and all subsequent absence windows contain independent noise.
        std::copy(signal.begin()+static_cast<std::ptrdiff_t>(delay+quarter*symbol/4),
                  signal.begin()+static_cast<std::ptrdiff_t>(delay+(quarter+1)*symbol/4),
                  fragment.begin()+static_cast<std::ptrdiff_t>(delay+quarter*symbol/4));
        for(const auto backend:{Backend::fft,Backend::compact}) {
            const auto baseline=receive(backend,c,fragment,false),drift=receive(backend,c,fragment,true);
            check(baseline.bits.empty()&&baseline.completions==0,
                  "isolated-quarter control must be rejected by the coherent baseline");
            check(drift.bits.empty()&&drift.completions==0,
                  "an isolated strong quarter or tail was promoted to a complete received bit");
        }
    }
}

void compact_budget_fallback_preserves_accumulation() {
    const auto c=config();const Bytes bits{0,0,0};const auto pcm=waveform(c,bits,Impairment::steady);
    modem::PatternCorrelator reference(c,search(false),workspace);
    const auto budget=reference.working_bytes()+128;
    modem::PatternCorrelator limited(c,search(true),budget),coherent(c,search(false),budget);
    check(!limited.drift_tolerant(),"insufficient optional workspace must preserve the complete coherent search bank");
    const auto a=receive_with(limited,c,pcm,257,false),b=receive_with(coherent,c,pcm,257,false);
    exact(a,bits,true);exact(b,bits,true);
    check(a.peak_working<=budget&&b.peak_working<=budget&&a.events.size()==b.events.size(),
          "constructor fallback changed memory bounds or exact progress");
    for(std::size_t i=0;i<a.events.size();++i)
        check(a.events[i].score==b.events[i].score,"constructor fallback relaxed coherent evidence thresholds");

    modem::PatternCorrelator shrinking(c,search(true),workspace),baseline(c,search(false),workspace);
    const auto prefix=static_cast<std::size_t>(modem::symbol_sample_count(c)/2);
    shrinking.push(std::span(pcm).first(prefix));baseline.push(std::span(pcm).first(prefix));
    check(shrinking.drift_tolerant()&&shrinking.take_bursts().empty()&&baseline.take_bursts().empty(),
          "workspace-shrink fixture must retain an unfinished whole-symbol accumulation");
    const auto reduced=baseline.working_bytes()+128;
    shrinking.set_workspace_bytes(reduced);baseline.set_workspace_bytes(reduced);
    check(!shrinking.drift_tolerant(),"workspace reduction must release optional drift state without replacing reception");
    const auto after=receive_with(shrinking,c,pcm,257,false,prefix),control=receive_with(baseline,c,pcm,257,false,prefix);
    exact(after,bits,true);exact(control,bits,true);
    check(after.peak_working<=reduced&&after.events.size()==control.events.size(),
          "workspace shrink changed the bounded pending stream");
    for(std::size_t i=0;i<after.events.size();++i)
        check(after.events[i].first_sample==control.events[i].first_sample&&
              after.events[i].end_sample==control.events[i].end_sample&&after.events[i].score==control.events[i].score,
              "dropping optional drift state discarded accumulated coherent samples or relaxed their evidence");
}

void chunk_boundaries_preserve_progress() {
    const Bytes bits{0,0,1};
    for(const auto backend:{Backend::fft,Backend::compact}) {
        const auto c=config(true);const auto pcm=waveform(c,bits,Impairment::wander_and_power);
        const auto small=receive(backend,c,pcm,true,37),large=receive(backend,c,pcm,true,257);
        // Large parallel pushes span quarter boundaries, exercising the
        // compact worker's persistent active-section state between tiles.
        const auto parallel=receive(backend,c,pcm,true,2048,false,3);
        exact(small,bits,true);exact(large,bits,true);exact(parallel,bits,true);
        for(const auto* compared:{&large,&parallel}) {
          check(small.events.size()==compared->events.size(),"PCM chunking or worker count changed drift reception event count");
          for(std::size_t i=0;i<small.events.size();++i) {
            const auto& a=small.events[i];const auto& b=compared->events[i];
            check(std::tie(a.bits,a.first_sample,a.end_sample,a.first_stream_symbol,a.complete,a.stream_phase_samples,
                           a.stream_first_sample,a.stream_first_symbol,a.missing_slots)==
                  std::tie(b.bits,b.first_sample,b.end_sample,b.first_stream_symbol,b.complete,b.stream_phase_samples,
                           b.stream_first_sample,b.stream_first_symbol,b.missing_slots),
                  "PCM chunking changed exact drift reception coordinates or pending identity");
            check(std::abs(a.score-b.score)<=1e-9*std::max({1.,std::abs(a.score),std::abs(b.score)}),
                  "PCM chunking changed whole-symbol drift evidence");
          }
        }
    }
}

void ineligible_profiles_retain_coherent_behavior() {
    const Bytes bits{0,0,1};
    auto short_profile=config(false,64);short_profile.bandwidth_hz=16; // Eight seconds, enough chips.
    auto few_chips=config(false,32);few_chips.bandwidth_hz=4; // Sixteen seconds, too few chips.
    auto tone=config(false);tone.spreading_mode=modem::SpreadingMode::tone;
    for(const auto backend:{Backend::fft,Backend::compact})for(const auto& c:{short_profile,few_chips,tone}) {
        auto pcm=waveform(c,bits,Impairment::steady);pcm.resize(pcm.size()+6*c.sample_rate);
        const auto enabled=receive(backend,c,pcm,true),coherent=receive(backend,c,pcm,false);
        exact(enabled,bits,true);exact(coherent,bits,true);
        check(enabled.events.size()==coherent.events.size(),"ineligible profiles unexpectedly activated extra drift fits");
        for(std::size_t i=0;i<enabled.events.size();++i)
            check(enabled.events[i].score==coherent.events[i].score,
                  "short, few-chip or tone profiles changed existing coherent evidence");
    }
}

void report_bounded_efficiency() {
    const Bytes bits{0,0,1};const auto c=config(true);const auto pcm=waveform(c,bits,Impairment::steady);
    for(const auto backend:{Backend::fft,Backend::compact}) {
        const auto coherent=receive(backend,c,pcm,false),drift=receive(backend,c,pcm,true);
        exact(coherent,bits,true);exact(drift,bits,true);
        std::cout<<(backend==Backend::fft?"FFT":"Compact")<<" identical-PCM comparison: coherent "
                 <<coherent.milliseconds<<" ms / "<<coherent.peak_working<<" bytes; drift "
                 <<drift.milliseconds<<" ms / "<<drift.peak_working<<" bytes\n";
    }
}

void independent_rank_and_selection_penalty() {
    using modem::detail::drift_evidence;
    using modem::detail::combine_drift_evidence;
    const auto close=[](double actual,double expected) {
        check(std::isfinite(actual)&&std::abs(actual-expected)<=2e-12*std::max(1.,std::abs(expected)),
              "drift evidence differs from its independent null-distribution reference");
    };
    for(const auto fraction:{.01,.25,.7,.99}) {
        // Integrating the Beta(4,2) density gives this explicit polynomial,
        // independent of the implementation's general log-sum recurrence.
        const auto remaining=1-fraction;
        const auto survival=remaining*remaining*(1+2*fraction+3*fraction*fraction+4*fraction*fraction*fraction);
        const auto expected=-std::log(survival);
        close(drift_evidence(fraction,1,6,4,false),expected);
        close(drift_evidence(fraction,1,12,4,true),expected);
        // One fitted complex coefficient is exactly the old coherent tail.
        close(drift_evidence(fraction,1,6,1,false),-5*std::log1p(-fraction));
        close(drift_evidence(fraction,1,8,1,true),-3*std::log1p(-fraction));
    }
    close(combine_drift_evidence(2,3,4),3-std::log(2.));
    close(combine_drift_evidence(4,2,4),4-std::log(2.));
    check(combine_drift_evidence(.1,.2,4)==0&&combine_drift_evidence(.125,50,1)==.125,
          "detector selection must charge both eligible alternatives and preserve an ineligible coherent score exactly");
    for(const auto invalid:{0U,5U})check(drift_evidence(.5,1,20,invalid,false)==0,
                                        "unsupported section counts must not supply evidence");
    check(drift_evidence(.5,1,8,4,true)==0&&drift_evidence(0,1,20,4,false)==0&&
          drift_evidence(.5,0,20,4,false)==0&&
          drift_evidence(std::numeric_limits<double>::infinity(),1,20,4,false)==0&&
          drift_evidence(.5,1,std::numeric_limits<double>::quiet_NaN(),4,false)==0,
          "rank-deficient, empty or nonfinite fits must not manufacture evidence");
    check(std::isfinite(drift_evidence(1,1,1e18,4,false))&&drift_evidence(1,1,1e18,4,false)>0,
          "very long complete observations must retain finite bounded evidence arithmetic");
    check(modem::detail::drift_boundary(3,std::numeric_limits<std::uint64_t>::max(),4)==13835058055282163711ULL,
          "section partitioning overflowed at a valid 64-bit observation endpoint");
    auto eligible=config(false,64);eligible.bandwidth_hz=8;
    check(modem::detail::drift_section_count(eligible)==4,"exactly sixteen seconds and sixteen chips per section must be eligible");
    eligible.integration_seconds=4099./256;
    check(modem::detail::drift_section_count(eligible)==1,
          "a truncated quarter with fewer than sixteen complete chips must keep the coherent detector");
}
}
int main() {
    try {
        independent_rank_and_selection_penalty();
        drifting_symbols_and_power_steps();drifting_signal_below_sample_noise();
        partial_symbols_and_eof_are_not_absence();
        reject_unrelated_waveforms();isolated_sections_are_not_whole_bits();
        chunk_boundaries_preserve_progress();compact_budget_fallback_preserves_accumulation();
        ineligible_profiles_retain_coherent_behavior();report_bounded_efficiency();
        std::cout<<"Sampled drift-tolerant pattern tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Sampled drift-tolerant pattern tests failed: "<<error.what()<<'\n';return 1;
    }
}
