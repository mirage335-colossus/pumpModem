#include "datapump/triad_modem.hpp"
#include "datapump/triad_simulation.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

using namespace datapump;
using namespace datapump::triad;
namespace {
void check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
template<class F> void rejects(F f, const char* message) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
Crypto fixture_key() {
    Bytes bytes(32);
    for (std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<std::uint8_t>(i*7+3);
    return Crypto(bytes);
}
void format_checks(const Crypto& key) {
    Format f;
    const auto g=geometry(f);
    check(g.frame_seconds>=8 && f.dwells_per_frame>=20,"minimum diversity duration");
    f.aggregate_rate_hz=3600; f.sample_rate=19200;
    const auto high=geometry(f);
    check(std::abs(high.chip_rate_hz-600)<1e-9,"one-third carrier rate");
    check(std::abs(high.occupied_bandwidth_hz-2250)<1e-9,"SSB ideal occupied width");
    f=Format{};
    for (const unsigned added : {1U,2U}) for (const bool interleave : {false,true}) {
        f.added_symbols=added; f.added_interleave=interleave;
        const auto cells=layout(f,4,key);
        for (std::size_t bit=0;bit<4;++bit) {
            std::set<unsigned> dwells;
            std::size_t total=0,worst=0;
            for (const auto& c:cells) if(c.bit==bit) {
                dwells.insert(c.dwell); ++total;
                const auto inside=c.center_seconds-std::floor(c.center_seconds/.4)*.4;
                check(inside>=.04 && inside<=.36,"cell escaped dwell margins");
            }
            check(dwells.size()>=20,"bit absent from diversity dwell");
            for (unsigned start_ms=40;start_ms<=350;++start_ms) {
                std::size_t lost=0;
                for(const auto& c:cells) if(c.bit==bit) {
                    const double within=std::fmod(c.center_seconds,.4)*1000;
                    if(within>=start_ms && within<start_ms+10) ++lost;
                }
                worst=std::max(worst,lost);
            }
            check(total>0 && worst*4<total,"10ms erasure disproportionately removes bit cells");
        }
    }
    f=Format{}; f.dwells_per_frame=19;
    rejects([&]{geometry(f);},"short diversity frame accepted");
    f=Format{}; f.sample_rate=1;
    rejects([&]{geometry(f);},"aliased carrier geometry accepted");
    f=Format{}; f.aggregate_rate_hz=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{geometry(f);},"nonfinite format accepted");
}
void waveform_and_receivers(const Crypto& key) {
    Format f;
    const std::vector<std::uint8_t> bits{0,0,1,1};
    const auto tx=transmit(bits,f,key);
    check(tx.source_bits==bits.size(),"exact source bit count");
    check(tx.source_end_sample>=8*f.sample_rate,"source duration too short");
    check(tx.mean_power>0 && std::isfinite(tx.peak_power),"waveform energy");
    // Measure actual shaped discriminating energy, not only cell counts.
    // Common cover and other bits cancel in these two transmitter templates.
    for(std::size_t bit=0;bit<bits.size();++bit) {
        auto opposite=bits;opposite[bit]^=1;
        const auto twin=transmit(opposite,f,key);
        const auto dwell_samples=static_cast<std::size_t>(std::llround(.4*f.sample_rate));
        std::vector<double> within(dwell_samples);
        double energy=0;
        for(std::size_t i=0;i<tx.samples.size();++i) {
            const auto e=std::norm(tx.samples[i]-twin.samples[i]);within[i%dwell_samples]+=e;energy+=e;
        }
        const auto lost_samples=static_cast<std::size_t>(std::llround(.010*f.sample_rate));
        double worst=0;
        for(std::size_t start=0;start+lost_samples<=within.size();++start) {
            double lost=0;for(std::size_t j=0;j<lost_samples;++j)lost+=within[start+j];
            worst=std::max(worst,lost/energy);
        }
        std::cout<<"bit "<<bit<<" worst repeated10ms discriminating-energy loss="<<worst<<'\n';
        check(worst<.12,"10ms window removes excessive shaped bit energy");
    }
    SearchPrior p; p.frequency_half_width_hz=0; p.threads=2;
    for(const auto mode:{Receiver::differential,Receiver::segmented_complex,Receiver::amplitude_only}) {
        const auto rx=receive(tx.samples,bits.size(),f,key,p,mode);
        std::cout<<receiver_name(mode)<<" noiseless score="<<rx.score<<" margin="<<rx.margin<<'\n';
        check(rx.bits==bits,"noiseless receiver failed exact leading-zero bits");
        check(rx.accepted,"noiseless receiver not admitted");
    }
    p.arithmetic=Arithmetic::fp32; p.threads=1;
    const auto a=receive(tx.samples,bits.size(),f,key,p,Receiver::differential);
    p.arithmetic=Arithmetic::fp64; p.threads=4;
    const auto b=receive(tx.samples,bits.size(),f,key,p,Receiver::differential);
    check(a.bits==b.bits && std::abs(a.score-b.score)<1e-4,"precision/thread mismatch");
    check(b.working_bytes<=p.memory_limit_bytes,"receiver exceeded declared workspace");
    std::vector<Complex> silence(tx.samples.size());
    check(!receive(silence,bits.size(),f,key,p,Receiver::differential).accepted,"silence admitted");
    bool partial_accepted=false;
    try {partial_accepted=receive(std::span(tx.samples).first(f.sample_rate/2),bits.size(),f,key,p,Receiver::differential).accepted;}
    catch(const std::exception&) {}
    check(!partial_accepted,"partial dwell capture admitted as a full eight-second bit");
    p.memory_limit_bytes=512*1024;p.frequency_half_width_hz=800;p.threads=2;
    const auto batched=receive(tx.samples,bits.size(),f,key,p,Receiver::differential);
    p.memory_limit_bytes=16*1024*1024;
    const auto unbatched=receive(tx.samples,bits.size(),f,key,p,Receiver::differential);
    check(batched.bits==unbatched.bits&&std::abs(batched.score-unbatched.score)<1e-12,"memory batching changed score/search");
    check(batched.hypotheses==unbatched.hypotheses,"memory batching omitted hypotheses");
    p.memory_limit_bytes=1;
    rejects([&]{receive(tx.samples,bits.size(),f,key,p,Receiver::differential);},"tiny workspace silently omits search");
    p=SearchPrior{}; p.maximum_hypotheses=1;
    rejects([&]{receive(tx.samples,bits.size(),f,key,p,Receiver::differential);},"hypothesis cap silently truncates search");
    f.encrypt=false;
    const auto unmasked=transmit(bits,f,key);
    check(unmasked.samples!=tx.samples,"encryption control did not alter samples");
    f=Format{}; f.added_symbols=2; f.added_interleave=true;
    const auto added=transmit(bits,f,key);
    p=SearchPrior{}; p.frequency_half_width_hz=0;
    check(receive(added.samples,bits.size(),f,key,p,Receiver::differential).bits==bits,"Added Symbols/interleave recovery");
    f=Format{}; f.bits_per_frame=2; f.burst_wait_frames=1;
    const auto burst=transmit(bits,f,key);
    check(burst.source_end_sample>=24*f.sample_rate,"burst compressed complete frames");
    check(receive(burst.samples,bits.size(),f,key,p,Receiver::differential).bits==bits,"burst recovery");
}
void channel_checks() {
    SimulationConfig c; c.sample_rate=6400; c.occupied_bandwidth_hz=750; c.noise_enabled=false;
    std::vector<Complex> x(6400);
    for(std::size_t i=0;i<x.size();++i) x[i]=std::polar(1.,2*std::numbers::pi*100*static_cast<double>(i)/c.sample_rate);
    const auto clean=simulate(x,c);
    check(clean.iq==x,"None/Off is not exact sample bypass");
    c.perturbation_slow_depth=.65; c.perturbation_fast_depth=.65;
    check(simulate(x,c).iq==x,"zero-Doppler perturbation is not bypass");
    c.perturbation_slow_depth=0;c.perturbation_fast_depth=0;
    c.tx.rf_ppm=.8; c.rx.rf_ppm=-.2;
    auto shifted=simulate(x,c);
    const auto phase=std::arg(shifted.iq[3201]*std::conj(shifted.iq[3200]));
    check(std::abs(phase*c.sample_rate/(2*std::numbers::pi)-130)<.02,"RF ppm frequency scaling");
    c.tx=Oscillator{}; c.rx=Oscillator{}; c.tx.audio_ppm=1000; c.rx.audio_ppm=-1000;
    shifted=simulate(x,c);
    const auto clockphase=std::arg(shifted.iq[3201]*std::conj(shifted.iq[3200]));
    check(std::abs(clockphase*c.sample_rate/(2*std::numbers::pi)-100*1.001/.999)<.03,"independent RX/TX audio scaling");
    c.tx=Oscillator{};c.rx=Oscillator{};
    std::vector<Complex> pulse(320);pulse[310]=1;
    c.spread_delay_ms=6;c.spread_doppler_hz=1;c.perturbation_fast_depth=.25;
    const auto tail=simulate(pulse,c);
    double tail_energy=0;for(std::size_t i=pulse.size();i<tail.iq.size();++i)tail_energy+=std::norm(tail.iq[i]);
    check(tail.iq.size()>pulse.size() && tail_energy>1e-6,"channel tail erased");
    check(simulate(pulse,c).iq==tail.iq,"seeded channel is not deterministic");
    c.spread_delay_ms=.5/c.sample_rate*1000;c.spread_doppler_hz=0;c.perturbation_fast_depth=0;
    std::vector<Complex> endpoint(64);endpoint.back()=1;
    const auto finite=simulate(endpoint,c);
    endpoint.resize(80);
    const auto padded=simulate(endpoint,c);
    double missing_energy=0;
    for(std::size_t i=finite.iq.size();i<padded.iq.size();++i)missing_energy+=std::norm(padded.iq[i]);
    check(missing_energy<1e-20,"fractional interpolation tail truncated");
    c=SimulationConfig{};c.sample_rate=6400;c.occupied_bandwidth_hz=750;c.noise_enabled=false;
    c.rf_hz=5.8e9;c.tx.rf_ppm=1;
    rejects([&]{simulate(x,c);},"microwave CFO aliased into narrow capture");
    check(std::abs(oscillator_bound_hz(5.8e9,1,8)-11600)<1e-9,"oscillator prior bound");
}
}
int main() {
    try {const auto key=fixture_key();format_checks(key);waveform_and_receivers(key);channel_checks();
        std::cout<<"triad checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"triad check failed: "<<e.what()<<'\n';return 1;}
}
