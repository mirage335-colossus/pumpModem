#include "datapump/triad_modem_v2.hpp"
#include "datapump/triad_simulation.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace datapump;
namespace t = datapump::triad;
namespace v = datapump::triad::v2;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool rejected=false;
    try { action(); } catch (const std::exception&) { rejected=true; }
    check(rejected,message);
}
Crypto key() {
    Bytes bytes(32);
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(3+7*i);
    return Crypto(bytes);
}
v::SearchPrior aligned() {
    v::SearchPrior p;
    p.frequency_half_width_hz=0;
    p.arrival_min_seconds=0;p.arrival_max_seconds=0;
    p.epoch_offsets_seconds={0};p.clock_offsets_ppm={0};
    p.threads=2;
    return p;
}
void geometry_checks(const Crypto& crypto) {
    v::Format f;
    auto g=v::geometry(f);
    check(std::abs(g.fine_chip_rate_hz-60)<1e-10,"Rate36 Added10 fine rate");
    check(std::abs(g.occupied_bandwidth_hz-225)<1e-10,"Rate36 occupied band");
    check(g.frame_seconds>=14.2222222222,"planner integration lost");
    check(g.payload_energy_seconds_per_bit>=f.minimum_bit_energy_seconds,
          "planner energy shared between bits");
    check(g.inner_coefficients_per_bit>=64,"inner pattern minimum lost");
    for(const auto rate:{36.,360.}) for(const auto interleave:{false,true}) {
        f.inner_rate_hz=rate;f.sample_rate=static_cast<std::uint32_t>(rate*10/6*16);
        f.minimum_bit_energy_seconds=rate==36?14.22222222222222:.35555555555556;
        f.added_interleave=interleave;
        g=v::geometry(f);
        check(g.frame_seconds>=8 && g.dwells_per_frame>=20,"minimum diversity calendar");
        const auto cells=v::layout(f,1,crypto);
        std::set<unsigned> dwells;
        std::map<std::size_t,std::set<unsigned>> groups;
        for(const auto& c:cells) {
            check(c.bit==0 && c.added_index<10,"cell bit/Added boundary");
            check(std::abs(c.center_seconds*g.fine_chip_rate_hz-
                (static_cast<double>(c.fine_index)+.5))<1e-7,"noninteger fine-cell permutation");
            dwells.insert(c.dwell);groups[c.logical_inner].insert(c.added_index);
        }
        check(dwells.size()>=20,"bit does not span twenty dwells");
        for(const auto& group:groups)check(group.second.size()==10,"partial Added Symbols group");
    }
    f.fhss=true;f.quiet_seconds=.055;f.ramp_seconds=2./3600;f.margin_seconds=.005;
    g=v::geometry(f);
    for(const auto& cell:v::layout(f,1,crypto)) {
        const auto within=std::fmod(cell.center_seconds,f.dwell_seconds);
        const auto guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/g.fine_chip_rate_hz;
        check(within>=guard-1e-10 && within<=f.dwell_seconds-guard+1e-10,
              "payload pulse escaped Fake FCC ramp margin");
    }
    const auto gated=v::transmit(std::vector<std::uint8_t>{1},f,crypto);
    check(std::norm(gated.samples[static_cast<std::size_t>(.01*f.sample_rate)])==0,
          "Fake FCC source quiet interval absent");
    t::SimulationConfig channel;channel.sample_rate=f.sample_rate;
    channel.occupied_bandwidth_hz=g.occupied_bandwidth_hz;channel.noise_enabled=false;
    channel.spread_delay_ms=1.5;channel.spread_doppler_hz=.3;
    const auto echoed=t::simulate(gated.samples,channel);
    const auto echo_index=static_cast<std::size_t>(std::llround(.346*f.sample_rate));
    check(std::norm(gated.samples[echo_index])==0 && std::norm(echoed.iq[echo_index])>1e-12,
          "propagated echo was gated again at the transmit guard");
    f=v::Format{};f.bits_per_frame=3;
    g=v::geometry(f);
    check(g.frame_seconds>=3*f.minimum_bit_energy_seconds,"three bits borrowed one bit's energy");
    f.inner_rate_hz=35.7;f.sample_rate=960;
    check(v::geometry(f).frame_seconds>=8,"noncommensurate dwell rate rejected");
    f=v::Format{};f.added_symbols=1;
    const auto unspread=v::layout(f,3,crypto);
    std::array<std::size_t,3> frame_counts{};
    for(const auto& cell:unspread)++frame_counts.at(cell.frame);
    check(frame_counts[0]==frame_counts[1] && frame_counts[1]==frame_counts[2],
          "fractional frame phase lost a complete Added-off pattern");
    f.sample_rate=1;
    rejects([&]{v::geometry(f);},"aliased sample rate accepted");
    // A narrow FHSS aperture can have a large calendar but few payload
    // coefficients. The workspace limit must cover preparation, too.
    f=v::Format{};f.inner_rate_hz=6e6;f.added_symbols=1;f.sample_rate=5'000'000;
    f.minimum_bit_energy_seconds=0;f.fhss=true;
    f.quiet_seconds=.1984;f.ramp_seconds=.0005;f.margin_seconds=.001;
    auto p=aligned();p.memory_limit_bytes=8*1024*1024;
    bool preparation_bounded=false;
    try {
        const auto bounded=v::receive(std::vector<v::Complex>(64),1,f,crypto,p,v::Receiver::coherent);
        preparation_bounded=!bounded.accepted && bounded.working_bytes>0 &&
            bounded.working_bytes<=p.memory_limit_bytes;
    } catch(const std::exception& error) {
        preparation_bounded=std::string(error.what()).find("memory")!=std::string::npos;
    }
    check(preparation_bounded,"large preparation calendar bypassed workspace accounting");
}
void recovery_checks(const Crypto& crypto) {
    v::Format f;
    const std::vector<std::uint8_t> bits{0};
    const auto tx=v::transmit(bits,f,crypto);
    check(tx.source_bits==1 && tx.mean_power>0,"transmitter exact source count/power");
    auto p=aligned();
    for(const auto mode:{v::Receiver::coherent,v::Receiver::differential,v::Receiver::amplitude_only}) {
        const auto rx=v::receive(tx.samples,1,f,crypto,p,mode);
        std::cout<<v::receiver_name(mode)<<" clean score="<<rx.score<<" margin="<<rx.margin<<'\n';
        check(rx.bits==bits && rx.accepted && rx.complete_observation,"noiseless receiver recovery");
        check(rx.working_bytes<=p.memory_limit_bytes,"workspace ceiling exceeded");
    }
    auto other=v::transmit(std::vector<std::uint8_t>{1},f,crypto);
    check(other.samples!=tx.samples,"identical bit waveforms");
    // Measure repeated erasures on the shaped distinguishing waveform, not
    // merely coefficient counts. Neither template is channel-normalized.
    const auto dwell=static_cast<std::size_t>(std::llround(.4*f.sample_rate));
    const auto loss=static_cast<std::size_t>(std::ceil(.010*f.sample_rate));
    std::vector<double> energy_by_dwell(dwell);
    double total=0;
    for(std::size_t i=0;i<tx.samples.size();++i) {
        const auto e=std::norm(tx.samples[i]-other.samples[i]);
        total+=e;energy_by_dwell[i%dwell]+=e;
    }
    double worst=0;
    for(std::size_t i=0;i+loss<=dwell;++i) {
        double e=0;for(std::size_t j=0;j<loss;++j)e+=energy_by_dwell[i+j];
        worst=std::max(worst,e/total);
    }
    std::cout<<"Rate36 worst repeated10ms distinguishing-energy loss="<<worst<<'\n';
    check(worst<.12,"repeated10ms disproportionately erases bit energy");

    p.arithmetic=t::Arithmetic::fp32;p.threads=1;
    const auto a=v::receive(tx.samples,1,f,crypto,p,v::Receiver::coherent);
    p.arithmetic=t::Arithmetic::fp64;p.threads=4;
    const auto b=v::receive(tx.samples,1,f,crypto,p,v::Receiver::coherent);
    check(a.bits==b.bits && std::abs(a.score-b.score)<1e-4,"precision/thread discrepancy");
    auto large=tx.samples;
    for(auto& sample:large)sample*=1e10;
    p.arithmetic=t::Arithmetic::automatic;
    const auto high=v::receive(large,1,f,crypto,p,v::Receiver::amplitude_only);
    check(high.accepted && high.bits==bits && high.arithmetic_used==t::Arithmetic::fp64,
          "automatic arithmetic missed fourth-power FP32 overflow");
    p.arithmetic=t::Arithmetic::fp32;
    rejects([&]{v::receive(large,1,f,crypto,p,v::Receiver::amplitude_only);},
            "forced FP32 accepted overflowing fourth-power workspace");
    auto small=tx.samples;
    for(auto& sample:small)sample*=1e-12;
    p.arithmetic=t::Arithmetic::automatic;
    const auto low=v::receive(small,1,f,crypto,p,v::Receiver::amplitude_only);
    check(low.accepted && low.bits==bits && low.arithmetic_used==t::Arithmetic::fp64,
          "receiver gain units changed amplitude-only recovery");
    p.arithmetic=t::Arithmetic::fp64;
    std::vector<v::Complex> zeros(tx.samples.size());
    check(!v::receive(zeros,1,f,crypto,p,v::Receiver::coherent).accepted,"zero input admitted");
    bool partial=false;
    try {partial=v::receive(std::span(tx.samples).first(f.sample_rate),1,f,crypto,p,v::Receiver::coherent).accepted;}
    catch(const std::exception&) {}
    check(!partial,"partial observation admitted");
    p.memory_limit_bytes=1;
    rejects([&]{v::receive(tx.samples,1,f,crypto,p,v::Receiver::coherent);},"workspace limit ignored");
    p=aligned();p.maximum_hypotheses=1;p.arrival_max_seconds=1;
    rejects([&]{v::receive(tx.samples,1,f,crypto,p,v::Receiver::coherent);},"search truncated to hypothesis cap");

    f.encrypt=false;
    check(v::transmit(bits,f,crypto).samples!=tx.samples,"encryption control ineffective");
    f=v::Format{};f.bits_per_frame=2;f.minimum_bit_energy_seconds=.2;f.burst_wait_frames=1;
    const std::vector<std::uint8_t> sequence{0,0,1};
    const auto burst=v::transmit(sequence,f,crypto);
    check(burst.source_end_sample>=3*v::geometry(f).frame_samples,"burst shortened frame calendar");
    p=aligned();
    check(v::receive(burst.samples,sequence.size(),f,crypto,p,v::Receiver::coherent).bits==sequence,
          "burst leading-zero and partial-final-frame recovery");
}
void acquisition_checks(const Crypto& crypto) {
    v::Format f;
    const std::vector<std::uint8_t> bits{1};
    const auto tx=v::transmit(bits,f,crypto);
    t::SimulationConfig c;
    c.sample_rate=f.sample_rate;c.occupied_bandwidth_hz=v::geometry(f).occupied_bandwidth_hz;
    c.noise_enabled=false;c.capture_lead_seconds=.5;c.capture_trail_seconds=.5;
    c.timing_offset_seconds=.037;c.static_doppler_hz=.11;
    const auto capture=t::simulate(tx.samples,c);
    auto rxformat=f;rxformat.epoch+=2;
    auto p=aligned();p.frequency_half_width_hz=.3;p.frequency_step_hz=.1;
    p.arrival_min_seconds=.4;p.arrival_max_seconds=.7;
    p.epoch_offsets_seconds={-3,-2,-1,0,1,2,3};
    const auto rx=v::receive(capture.iq,1,rxformat,crypto,p,v::Receiver::coherent);
    std::cout<<"searched score="<<rx.score<<" arrival="<<rx.timing_seconds
        <<" epoch="<<rx.epoch_offset_seconds<<" hypotheses="<<rx.hypotheses<<'\n';
    check(rx.accepted && rx.bits==bits,"searched timing/CFO/epoch recovery");
    check(rx.epoch_offset_seconds==-2,"wrong epoch selected");
    check(std::abs(rx.timing_seconds-.537)<.01,"arrival estimate outside fine aperture");
    f.minimum_bit_energy_seconds=16;
    const auto exact=v::transmit(bits,f,crypto);
    p=aligned();
    check(v::receive(exact.samples,1,f,crypto,p,v::Receiver::coherent).accepted,
          "floating calendar duration rejected exact complete capture");
    p.arrival_min_seconds=.0001;p.arrival_max_seconds=.0001;
    check(v::receive(exact.samples,1,f,crypto,p,v::Receiver::coherent).accepted,
          "outward-rounded complete arrival discarded before grid search");
}
}
int main() {
    try {const auto crypto=key();geometry_checks(crypto);recovery_checks(crypto);acquisition_checks(crypto);
        std::cout<<"triad v2 checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"triad v2 failed: "<<error.what()<<'\n';return 1;}
}
