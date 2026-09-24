#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/channel.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <string>
#include <tuple>

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
    // These fixtures describe a capture starting at the payload clock, with
    // neither hardware settling nor outer filter tails available to acquire.
    const auto padding=static_cast<std::ptrdiff_t>(modem::pattern_pulse_padding_samples(c));
    analytic=std::vector<std::complex<double>>(analytic.begin()+padding,analytic.end()-padding);
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
        for(auto& burst:bursts) {
            if(burst.missing_slots){burst.bits.assign(burst.missing_slots,modem::missing_pattern_bit);burst.missing_slots=0;}
            if(!result.bursts.empty() && result.bursts.back().stream_first_sample==burst.stream_first_sample &&
               result.bursts.back().stream_first_symbol==burst.stream_first_symbol &&
               result.bursts.back().first_stream_symbol+result.bursts.back().bits.size()==burst.first_stream_symbol) {
                auto& prior=result.bursts.back();prior.bits.insert(prior.bits.end(),burst.bits.begin(),burst.bits.end());
                prior.complete=burst.complete;prior.end_sample=burst.end_sample;prior.score=burst.score;prior.stream_phase_samples=burst.stream_phase_samples;
                prior.support_samples=burst.support_samples;
                prior.frequency_hz=burst.frequency_hz;
            } else result.bursts.push_back(std::move(burst));
        }
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
    return result.bursts.front();
}
bool same_burst(const modem::PatternBurst& a,const modem::PatternBurst& b) {
    return std::tie(a.bits,a.first_sample,a.end_sample,a.first_stream_symbol,a.frequency_hz,a.score,
                    a.complete,a.stream_phase_samples,a.stream_first_sample,a.stream_first_symbol,
                    a.missing_slots,a.support_samples)==
           std::tie(b.bits,b.first_sample,b.end_sample,b.first_stream_symbol,b.frequency_hz,b.score,
                    b.complete,b.stream_phase_samples,b.stream_first_sample,b.stream_first_symbol,
                    b.missing_slots,b.support_samples);
}
bool same_evidence(const modem::PatternEvidence& a,const modem::PatternEvidence& b) {
    return std::tie(a.first_sample,a.end_sample,a.stream_symbol,a.frequency_hz,a.score,a.alternative_score,
                    a.bit,a.stream_phase_samples,a.admission_threshold)==
           std::tie(b.first_sample,b.end_sample,b.stream_symbol,b.frequency_hz,b.score,b.alternative_score,
                    b.bit,b.stream_phase_samples,b.admission_threshold);
}
struct ComparedProgress {
    Bytes bits;
    std::size_t pending_polls=0,complete_events=0,candidates=0;
    std::size_t completed_at=0;
};
ComparedProgress compare_parallel_progress(const std::vector<float>& samples,const modem::Config& c,
                                          modem::PatternSearch search,unsigned workers,bool shrink=false) {
    constexpr std::size_t workspace=2*1024*1024,reduced=256*1024;
    search.candidate_limit=31;search.track_limit=4;search.bit_limit=128;
    search.worker_threads=1;modem::PatternReceiver serial(c,workspace,search);
    search.worker_threads=workers;modem::PatternReceiver parallel(c,workspace,search);
    ComparedProgress result;
    std::size_t budget=workspace;
    const auto poll=[&](std::size_t position) {
        check(serial.working_bytes()<=budget && parallel.working_bytes()<=budget,
              "parallel search exceeded the current workspace ceiling");
        if(serial.working_bytes()!=parallel.working_bytes())
            throw Error("worker count changed idle memory available to competing receive searches at "+
                        std::to_string(position)+" samples (serial "+std::to_string(serial.working_bytes())+
                        ", parallel "+std::to_string(parallel.working_bytes())+", "+std::to_string(workers)+
                        " workers, "+std::to_string(c.spreading_factor)+" chips, "+std::to_string(c.sample_rate)+
                        " Hz, private "+std::to_string(c.scramble)+", phase bank "+std::to_string(search.search_stream_phases)+")");
        check(serial.acquiring()==parallel.acquiring() && serial.synchronized()==parallel.synchronized() &&
              serial.clock_windowed()==parallel.clock_windowed(),
              "worker count changed search status at a progress poll");
        check(same_burst(serial.provisional(),parallel.provisional()),
              "worker count changed an exact provisional prefix or its evidence");
        for(const auto limit:{search.candidate_limit,std::size_t{3}}) {
            const auto a=serial.candidates(limit),b=parallel.candidates(limit);
            check(a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),same_evidence),
                  "worker count changed ordered candidate fields or admission thresholds");
            result.candidates=std::max(result.candidates,a.size());
        }
        const auto a=serial.diagnostics(),b=parallel.diagnostics();
        check(std::tie(a.sample_offset,a.preamble_correlation,a.snr_db,a.bit_rate,a.constellation,a.waveform,a.pattern_score)==
              std::tie(b.sample_offset,b.preamble_correlation,b.snr_db,b.bit_rate,b.constellation,b.waveform,b.pattern_score) &&
              a.preamble_reception.has_value()==b.preamble_reception.has_value(),
              "worker count changed diagnostic values at a progress poll");
        if(a.preamble_reception) {
            const auto& x=*a.preamble_reception;const auto& y=*b.preamble_reception;
            check(std::tie(x.expected_samples,x.observed_samples,x.matched_samples)==
                  std::tie(y.expected_samples,y.observed_samples,y.matched_samples),
                  "worker count changed observed preamble coverage");
        }
        const auto first=serial.take_bursts(),second=parallel.take_bursts();
        check(first.size()==second.size() && std::equal(first.begin(),first.end(),second.begin(),same_burst),
              "worker count delayed a pending bit or changed a burst event");
        for(const auto& event:first) {
            result.bits.insert(result.bits.end(),event.bits.begin(),event.bits.end());
            result.bits.insert(result.bits.end(),event.missing_slots,modem::missing_pattern_bit);
            if(!event.complete && (!event.bits.empty() || event.missing_slots))++result.pending_polls;
            if(event.complete){++result.complete_events;result.completed_at=position;}
        }
        check(serial.take_chip_constellation()==parallel.take_chip_constellation(),
              "worker count changed drainable chip points at a progress poll");
    };
    poll(0);
    constexpr std::array<std::size_t,5> chunks{1,137,19,503,71};
    for(std::size_t position=0,chunk=0;position<samples.size();++chunk) {
        const auto count=std::min(chunks[chunk%chunks.size()],samples.size()-position);
        const auto block=std::span(samples).subspan(position,count);
        serial.push(block);parallel.push(block);position+=count;
        poll(position);
        if(shrink && budget==workspace && position>samples.size()/3) {
            serial.set_workspace_bytes(reduced);parallel.set_workspace_bytes(reduced);budget=reduced;
            poll(position);
        }
    }
    serial.finish();parallel.finish();poll(samples.size());
    serial.finish();parallel.finish();poll(samples.size());
    return result;
}
void parallel_search_exact_progress() {
    for(const unsigned workers:{3U,0U}) {
        for(const bool keyed:{false,true}) {
            const auto c=config(64,keyed);const auto symbol=modem::symbol_sample_count(c);
            const auto step=.25*c.sample_rate/static_cast<double>(symbol);
            modem::PatternSearch search;
            // Duplicate offsets produce exact score ties. Search order must
            // still select the same start/stream address and evidence record.
            search.frequency_offsets_hz={0,0,-step,step,-step};
            const auto result=compare_parallel_progress(waveform(c,{0,0,1},137,3*symbol,.73,.03),
                                                        c,search,workers,true);
            check(result.bits==Bytes({0,0,1}) && result.pending_polls>0 && result.candidates>0,
                  "parallel comparison must exercise leading zeros, partial bytes and pending evidence");
            check(result.complete_events==0,"parallel search treated EOF as physical completion");
            std::vector<float> quiet(4*symbol);
            search.retain_score=0;
            const auto tied=compare_parallel_progress(quiet,c,search,workers);
            check(tied.bits.empty() && tied.complete_events==0,
                  "parallel tied background manufactured payload or completion");
            std::mt19937_64 random(731);std::normal_distribution<float> noise;
            for(auto& sample:quiet)sample=noise(random);
            const auto background=compare_parallel_progress(quiet,c,search,workers);
            check(background.bits.empty(),"parallel noise-only comparison admitted a payload");
        }
        auto short_private=config(16,true);short_private.dsss=true;short_private.dsss_seed[11]=139;
        short_private.sample_rate=48000;short_private.bandwidth_hz=12000;short_private.carrier_hz=9000;
        const auto short_symbol=modem::symbol_sample_count(short_private);
        const auto short_result=compare_parallel_progress(waveform(short_private,{0,0,1},137,3*short_symbol,.31,.001),
                                                          short_private,{},workers,true);
        check(short_result.bits==Bytes({0,0,1}),"parallel sample-resolution private fit lost exact short bits");

        for(const bool split_initial_phases:{false,true}) {
            auto phase=config(split_initial_phases?128:16,true);phase.pulse_shaping=false;
            if(split_initial_phases)phase.integration_seconds=.7;
            else {phase.sample_rate=8000;phase.bandwidth_hz=1000;}
            const auto symbol=modem::symbol_sample_count(phase);
            const auto step=std::gcd(symbol,static_cast<std::uint64_t>(phase.sample_rate));
            phase.stream_phase_samples=(std::min(symbol,static_cast<std::uint64_t>(phase.sample_rate))-1)/step*step;
            Bytes bits{0,0,1,0,1,1,0,1,0,1,1,0,1,0,0,1};const auto block=bits;
            if(!split_initial_phases)for(unsigned i=0;i<2;++i)bits.insert(bits.end(),block.begin(),block.end());
            const auto samples=waveform(phase,bits,137,3*symbol,.37,.01);phase.stream_phase_samples=0;
            modem::PatternSearch search;search.search_stream_phases=true;
            const auto phased=compare_parallel_progress(samples,phase,search,workers);
            check(phased.bits==bits,"parallel phase bank changed timestamp-dependent private bits");
        }
    }
}
void parallel_search_physical_absence() {
    auto c=config(32,true);c.pulse_shaping=false;
    c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;c.integration_seconds=1;
    const Bytes bits{0,0,1};const auto symbol=modem::symbol_sample_count(c);
    constexpr std::size_t delay=17;
    const auto samples=waveform(c,bits,delay,12*c.sample_rate,.37);
    for(const unsigned workers:{3U,0U}) {
        const auto result=compare_parallel_progress(samples,c,{},workers,true);
        check(result.bits==bits && result.pending_polls>=2 && result.complete_events==1,
              "parallel search must expose pending partial bytes and exactly one physical completion");
        check(result.completed_at>=delay+bits.size()*symbol+modem::pattern_absence_samples(c),
              "parallel search completed before six seconds of fully scored absence");
    }
}
void exact_blind_bits() {
    constexpr std::array<std::size_t,6> chunks{1,7,131,19,503,47};
    for(bool keyed:{false,true})for(double phase:{.27,1.73,3.11}) {
        const auto c=config(128,keyed);const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const auto result=receive(waveform(c,{0,0,1},137,2*symbol,phase,.03),c,chunks);
        const auto& burst=exact(result,{0,0,1});
        check(burst.first_sample>=127 && burst.first_sample<=147,"blind start estimate missed arbitrary PCM delay");
        check(burst.end_sample>=137+3*symbol-10 && burst.end_sample<=137+3*symbol+10,"raw burst gained training or a padding symbol");
        double previous=-std::log(modem::PatternSearch{}.false_alarm_probability);
        check(!result.candidates.empty(),"received symbols must expose their retained diagnostic evidence");
        for(const auto& candidate:result.candidates) {
            check(std::isfinite(candidate.admission_threshold) && candidate.admission_threshold>=previous,
                  "retained FFT evidence must carry the finite admission reference at its search position");
            previous=candidate.admission_threshold;
        }
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
void high_bandwidth_short_patterns() {
    constexpr std::array<std::size_t,3> chunks{13,97,7};
    const Bytes bits{0,1,0,0,1};
    for(unsigned chips:{3U,4U,6U,8U,12U,16U})for(unsigned mode=0;mode<4;++mode) {
        auto c=config(chips,(mode&1U)!=0);c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
        c.sample_rate=48000;c.bandwidth_hz=12000;c.carrier_hz=9000;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        // All eight start residues within a chip, with independent carrier
        // phases. The private masks change at every absolute chip address.
        for(std::size_t delay=135;delay<143;++delay)for(double phase:{.27,1.73,3.11}) {
            try {
                const auto result=receive(waveform(c,bits,delay,3*symbol,phase,.001),c,chunks);
                const auto& burst=exact(result,bits);
                check(burst.first_sample+2>=delay && burst.first_sample<=delay+2 &&
                      burst.end_sample==delay+bits.size()*symbol,
                      "short private patterns must preserve start and final-bit timing");
            } catch(const Error& error) {
                throw Error(std::to_string(chips)+" chips / mode "+std::to_string(mode)+
                            " / delay "+std::to_string(delay)+" / phase "+std::to_string(phase)+": "+error.what());
            }
        }
    }
}
void short_private_noise_evidence() {
    constexpr std::array<std::size_t,3> chunks{137,997,53};
    for(unsigned mode=1;mode<4;++mode) {
        auto c=config(16,(mode&1U)!=0);c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
        c.sample_rate=48000;c.bandwidth_hz=12000;c.carrier_hz=9000;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        Bytes bits(2048);std::mt19937_64 random(13);
        for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1U);
        const auto samples=waveform(c,bits,137,3*symbol,.73,.001);
        auto wrong=c;
        if(mode&1U)wrong.spreading_seed[7]^=0x80;else wrong.dsss_seed[7]^=0x80;
        check(receive(samples,wrong,chunks).bursts.empty(),
              "sample-resolution private patterns must not inflate a wrong key into evidence");
        // A later second inside this capture is intentionally acquirable.
        // Choose an epoch beyond every symbol in the generated waveform.
        wrong=c;wrong.stream_epoch+=1+samples.size()/c.sample_rate;
        check(receive(samples,wrong,chunks).bursts.empty(),
              "sample-resolution private patterns must reject a wrong stream epoch");
        std::normal_distribution<double> noise(0,std::sqrt(modem::nominal_signal_power));
        std::vector<float> background(4096*symbol);
        for(auto& sample:background)sample=static_cast<float>(noise(random));
        check(receive(background,c,chunks).bursts.empty(),
              "sample-resolution private patterns must reject real Gaussian background");
        // A later clean span cannot retroactively confirm an obscured first
        // bit, and its score cannot justify extra bits from a noisy tail.
        auto bounded=waveform(c,{1,0,0,1},137,32*symbol,.73);
        std::mt19937_64 obscured(197);std::normal_distribution<double> strong_noise(0,2.);
        for(std::size_t i=0;i<137+symbol;++i)bounded[i]+=static_cast<float>(strong_noise(obscured));
        for(std::size_t i=137+4*symbol;i<bounded.size();++i)bounded[i]=static_cast<float>(noise(obscured));
        const auto recovered=receive(bounded,c,chunks);
        const auto& burst=exact(recovered,{0,0,1});
        check(burst.first_sample+2>=137+symbol && burst.end_sample<=137+4*symbol+2,
              "short private confidence must belong only to the measured payload span");
    }
}
void private_template_energy_normalization() {
    // Select a valid secret template with substantially less than unit mean
    // energy. A perfectly matching observation must still explain essentially
    // all its energy; using the number of chips as the norm penalizes this key.
    for(unsigned mode=1;mode<=3;++mode) {
        auto c=config(16,(mode&1U)!=0);c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
        // This exact-fit regression isolates template-energy normalization.
        // A shaped symbol also contains unknown neighboring-symbol overlap;
        // its confidence is checked separately against raw-sample Gram scores.
        c.pulse_shaping=false;
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
    check(first.first_sample==second.first_sample && first.end_sample==second.end_sample && first.score==second.score &&
          first.support_samples==second.support_samples && first.support_samples>0 &&
          first.support_samples<=3*static_cast<double>(symbol),
          "PCM push chunk boundaries changed acquisition evidence");
}
void carrier_evidence_recovers_after_distorted_start() {
    auto c=config(16);c.sample_rate=14400;c.bandwidth_hz=3600;c.carrier_hz=1500;c.spreading_seed.fill(0);
    const auto symbol=modem::symbol_sample_count(c),padding=modem::pattern_pulse_padding_samples(c);
    Bytes bits;for(unsigned i=0;i<64;++i)bits.push_back(static_cast<std::uint8_t>((i+i/3)&1));
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(tx.total_samples());tx.read_analytic(analytic);
    const auto step=.25*c.sample_rate/static_cast<double>(symbol);
    for(const bool persistent_offset:{false,true}) {
        std::vector<float> samples(137+analytic.size()+8*c.sample_rate);
        for(std::size_t i=0;i<analytic.size();++i) {
            // The transient case disturbs only startup. The persistent case
            // verifies that correcting a stale label never forces the center.
            const auto shifted=persistent_offset?i:std::min<std::uint64_t>(i,padding+symbol);
            const auto phase=-2*std::numbers::pi*step*static_cast<double>(shifted)/c.sample_rate;
            samples[137+i]=static_cast<float>((analytic[i]*std::polar(1.,phase)).real());
        }
        for(const bool clock_window:{false,true})for(const auto chunk:std::array<std::size_t,2>{37,2048}) {
            modem::PatternSearch search;search.chunk_bits=16;
            search.compact_clock_search=clock_window;
            search.start_offset_seconds=static_cast<double>(137+padding)/c.sample_rate;
            const std::array<std::size_t,1> chunks{chunk};
            const auto result=receive(samples,c,chunks,search);const auto& burst=exact(result,bits);
            check(burst.complete,"carrier correction must preserve the six-second physical end");
            if(std::abs(burst.frequency_hz-(c.carrier_hz-(persistent_offset?step:0)))>=1e-8)
                throw Error("carrier label must follow accumulated pattern evidence, including a real off-center signal: "+
                    std::to_string(persistent_offset)+"/"+std::to_string(clock_window)+"/"+std::to_string(chunk)+"@"+std::to_string(burst.frequency_hz));
        }
    }
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
void pending_tail_requires_joint_confidence() {
    auto c=config(64,true);c.pulse_shaping=false;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.initial_stream_symbols=1;
    search.false_alarm_probability=1e-8; // This fixture brackets the original 30.5/34 evidence boundary.
    constexpr std::array<std::size_t,3> changing{137,503,17};
    for(const bool joint_confident:{false,true}) {
        auto samples=waveform(c,{0,1,0},0,3*symbol,.73);
        std::mt19937_64 random(197);std::normal_distribution<double> noise(0,2.);
        for(std::size_t i=symbol;i<2*symbol;++i)samples[i]+=static_cast<float>(noise(random));
        if(!joint_confident) {
            std::mt19937_64 last_random(821);std::normal_distribution<double> last_noise(0,1.);
            for(std::size_t i=2*symbol;i<3*symbol;++i)samples[i]+=static_cast<float>(last_noise(last_random));
        }
        const std::array<std::size_t,1> whole{samples.size()};
        for(const auto chunks:{std::span<const std::size_t>(changing),std::span<const std::size_t>(whole)}) {
            const auto result=receive(samples,c,chunks,search);
            // Middle evidence is about 9.3. A last-symbol score around 34 meets
            // standalone confidence but leaves the joint tail bound around 30.5,
            // below acceptance. A clean last symbol also establishes joint
            // confidence, which must preserve the existing combined-chain behavior.
            check(std::any_of(result.candidates.begin(),result.candidates.end(),[&](const auto& e) {
                return e.stream_symbol==1 && e.score>=search.retain_score && e.score<10 &&
                    e.score-e.alternative_score>=1;
            }),"weak-tail fixture must retain an individually unconfirmed middle-symbol candidate");
            if(joint_confident) {
                check(exact(result,{0,1,0}).first_stream_symbol==0,
                      "valid combined confidence must preserve the pending tail and entire admitted span");
                continue;
            }
            auto preserve=search;
            const auto joined=receive(samples,c,chunks,preserve);
            check(joined.bursts.size()==1 && joined.bursts.front().bits==Bytes({0,modem::missing_pattern_bit,0}),
                  "packet gap preservation must replace an unsupported weak tail without borrowing later confidence");
        }
    }
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
void timed_gap_cannot_veto_independent_start() {
    auto c=config(64);c.pulse_shaping=false;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto start=4*symbol+symbol/3;
    const Bytes later{0,0,1};auto samples=waveform(c,later,start,3*symbol,.73);
    const auto earlier=waveform(c,{1,0},0,0,.37);
    std::copy(earlier.begin(),earlier.end(),samples.begin());
    modem::PatternSearch search;
    for(const auto chunk:std::array<std::size_t,2>{37,samples.size()}) {
        const std::array<std::size_t,1> chunks{chunk};
        const auto result=receive(samples,c,chunks,search);
        check(result.bursts.size()==2 && result.bursts.front().bits==Bytes({1,0}) &&
              result.bursts.back().bits==later,
              "an obscured retained clock must not veto a confident burst on independent timing");
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
void keyed_track_survives_missing_symbols() {
    auto c=config(256,true);c.pulse_shaping=false;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const Bytes before{0,1,1,0},after{1,0,0,1};
    Bytes bits=before;bits.insert(bits.end(),{0,0,0});bits.insert(bits.end(),after.begin(),after.end());
    auto samples=waveform(c,bits,137,3*symbol,.37,.02);
    std::fill(samples.begin()+static_cast<std::ptrdiff_t>(137+4*symbol),
              samples.begin()+static_cast<std::ptrdiff_t>(137+7*symbol),0.F);
    constexpr std::array<std::size_t,3> chunks{127,19,503};
    modem::PatternSearch preserve;
    auto expected=bits;std::fill(expected.begin()+4,expected.begin()+7,modem::missing_pattern_bit);
    for(const auto chunk:std::array<std::size_t,2>{17,samples.size()}) {
        const std::array<std::size_t,1> delivery{chunk};
        const auto joined=receive(samples,c,delivery,preserve);
        check(joined.bursts.size()==1 && joined.bursts.front().bits==expected &&
              joined.bursts.front().first_stream_symbol==0,
              "timed gaps must keep unknown interior slots and trim trailing silence across chunk sizes");
    }
    preserve.bit_limit=5;
    const auto limited=receive(samples,c,chunks,preserve);
    check(limited.bursts.size()==1 && limited.bursts.front().bits==expected,
          "compact unknown runs and drained decisions must preserve gaps within the bounded bit capacity");
}
void default_gap_timeout_ends_active_message() {
    auto c=config(32,true);c.pulse_shaping=false;
    c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;c.integration_seconds=1;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const Bytes bits{1,0,1,1};constexpr std::size_t delay=17,workspace=1024*1024;
    const auto end=delay+bits.size()*symbol;
    const auto samples=waveform(c,bits,delay,12*c.sample_rate,.37);
    modem::PatternSearch search;search.frequency_offsets_hz={0};
    modem::PatternReceiver receiver(c,workspace,search);
    check(!receiver.clock_windowed(),"gap timeout fixture must exercise the FFT path");
    const auto at_limit=end+5*c.sample_rate;
    receiver.push(std::span(samples).first(at_limit));
    check(receiver.synchronized() && receiver.provisional().bits==bits,
          "five seconds of missing slots must retain the active clock without publishing a final event");
    const auto pending=receiver.take_bursts();
    check(pending.size()==1 && pending.front().bits==bits && !pending.front().complete,
          "confirmed FFT decisions must be visible while the six-second absence window is pending");
    const auto expired_at=at_limit+2*symbol;
    receiver.push(std::span(samples).subspan(at_limit,expired_at-at_limit));
    check(!receiver.synchronized() && !receiver.acquiring(),
          "continued missing symbols beyond six seconds must discard the active FFT track");
    const auto completed=receiver.take_bursts();
    check(completed.size()==1 && completed.front().complete && completed.front().bits.empty(),
          "gap timeout must complete the confirmed message without repeating its drained bits or adding an unobserved tail");
    const auto idle_bytes=receiver.working_bytes();
    receiver.push(std::span(samples).subspan(expired_at));
    check(receiver.take_bursts().empty() && receiver.working_bytes()<=idle_bytes && idle_bytes<=workspace,
          "continued silence must not grow message storage or republish the expired span");
}
void independently_started_epoch_recovers_phase() {
    for(unsigned profile=0;profile<3;++profile) {
        auto transmitter=config(128,true);transmitter.pulse_shaping=false;
        if(profile==1)transmitter.integration_seconds=.7;
        if(profile==2) {
            transmitter=config(16,true);transmitter.pulse_shaping=false;
            transmitter.sample_rate=8000;transmitter.bandwidth_hz=1000;
        }
        const auto symbol=modem::symbol_sample_count(transmitter);
        const auto step=std::gcd(symbol,static_cast<std::uint64_t>(transmitter.sample_rate));
        transmitter.stream_phase_samples=(std::min(symbol,static_cast<std::uint64_t>(transmitter.sample_rate))-1)/step*step;
        Bytes bits{0,1,1,0,0,1,0,1,0,1,1,0,1,0,0,1};
        if(profile==2) {
            // Initial cached templates are phase-independent, but later
            // symbols cross the second at different nonzero sample phases.
            const auto block=bits;for(unsigned i=0;i<3;++i)bits.insert(bits.end(),block.begin(),block.end());
        }
        const auto samples=waveform(transmitter,bits,137,3*symbol,.37,.01);
        auto receiver=transmitter;receiver.stream_phase_samples=0;
        modem::PatternSearch search;search.search_stream_phases=true;search.frequency_offsets_hz={0};
        constexpr std::array<std::size_t,3> chunks{127,19,503};
        const auto result=receive(samples,receiver,chunks,search);
        const auto& burst=exact(result,bits);
        check(burst.first_stream_symbol==0,"an independently admitted second must start at its own first symbol");
        for(std::size_t index=0;index<bits.size();++index) {
            const auto expected=modem::symbol_stream_address(transmitter.stream_epoch,transmitter.stream_phase_samples,
                index,symbol,transmitter.sample_rate);
            const auto actual=modem::symbol_stream_address(receiver.stream_epoch,burst.stream_phase_samples,
                index,symbol,receiver.sample_rate);
            check(actual.epoch==expected.epoch && actual.ordinal==expected.ordinal,
                  "recovered phase must retain every decoded symbol's exact timestamp and within-second counter");
        }
    }
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
void high_snr_sampled_channel() {
    // At 12 kHz bandwidth these sample SNRs correspond to 30 and 24 dB
    // in-band SNR. Seed 13 previously exposed lost first/final bits at eight
    // chips, even with a substantially stronger channel. Exercise the
    // conservative shorter profiles against those independent clock/phase
    // and fractional startup effects, including the whole settling prefix.
    for(unsigned chips:{16U,32U})for(unsigned mode=0;mode<4;++mode) {
        auto c=config(chips,(mode&1U)!=0);c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
        c.sample_rate=48000;c.bandwidth_hz=12000;c.carrier_hz=9000;
        Bytes bits(1536);std::mt19937 random(731);
        for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1U);
        modem::StreamingTransmitter transmitter(modem::RawBits{bits},c);
        modem::ChannelConfig impairment;impairment.snr_db=chips==16?27:21;
        impairment.clock_error_ppm=100;impairment.phase_noise_degrees_per_sqrt_second=.5;impairment.seed=13;
        modem::SampledSimulationChannel channel(c,impairment);
        std::array<float,509> block{};std::vector<float> samples;
        while(const auto count=channel.read(transmitter,block))
            samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        const auto offset=samples.size();
        samples.resize(offset+3*modem::symbol_sample_count(c));channel.read_noise(std::span(samples).subspan(offset));
        constexpr std::array<std::size_t,4> chunks{137,503,17,1021};
        try {exact(receive(samples,c,chunks),bits);}
        catch(const Error& error){throw Error(std::to_string(chips)+" chips / mode "+std::to_string(mode)+": "+error.what());}
    }
}
void orthogonal_private_pattern_bins() {
    constexpr std::array<std::size_t,3> chunks{13,97,7};
    const Bytes short_bits{0,1,0,0,1};
    for(unsigned rate:{6000U,8000U})for(unsigned mode=1;mode<4;++mode) {
        auto c=config(32,(mode&1U)!=0);c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
        c.sample_rate=rate;c.bandwidth_hz=1000;c.carrier_hz=rate==6000?1500:2000;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        const auto chip=static_cast<std::size_t>(modem::pattern_chip_samples(c));
        // Orthogonal private bins retain their compact established fit.
        // Automatic profiles use at least 32 chips here: 16-chip patterns can
        // lose a boundary bit at some chip residues even in a clean channel.
        for(std::size_t delay=135;delay<135+chip;++delay)for(double phase:{.27,1.73,3.11}) {
            const auto result=receive(waveform(c,short_bits,delay,3*symbol,phase,.001),c,chunks);
            const auto& burst=exact(result,short_bits);
            check(burst.first_sample+chip/2>=delay && burst.first_sample<=delay+chip/2 &&
                  burst.end_sample+chip/2>=delay+short_bits.size()*symbol &&
                  burst.end_sample<=delay+short_bits.size()*symbol+chip/2,
                  "orthogonal private bins lost short-message endpoint timing");
        }
        Bytes bits(1536);std::mt19937 random(731);
        for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1U);
        modem::StreamingTransmitter transmitter(modem::RawBits{bits},c);
        modem::ChannelConfig impairment;impairment.snr_db=24-10*std::log10(rate/2000.);
        impairment.clock_error_ppm=100;impairment.phase_noise_degrees_per_sqrt_second=.5;impairment.seed=13;
        modem::SampledSimulationChannel channel(c,impairment);
        std::array<float,509> block{};std::vector<float> samples;
        while(const auto count=channel.read(transmitter,block))
            samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        const auto offset=samples.size();samples.resize(offset+3*symbol);
        channel.read_noise(std::span(samples).subspan(offset));
        try {exact(receive(samples,c,chunks),bits);}
        catch(const Error& error){throw Error(std::to_string(rate)+" Hz / mode "+std::to_string(mode)+": "+error.what());}
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
    limited.push(samples);limited.finish();Bytes limited_bits;
    for(const auto& event:limited.take_bursts())limited_bits.insert(limited_bits.end(),event.bits.begin(),event.bits.end());
    check(limited_bits==Bytes({0,0,1}),"a full decision chunk must drain instead of ending a valid signal");
}
void short_pattern_shared_projection_phase() {
    for(unsigned mode=0;mode<4;++mode) {
    auto c=config(16);c.carrier_hz=1500;
    c.scramble=(mode&1U)!=0;c.dsss=(mode&2U)!=0;c.dsss_seed[11]=139;
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
}
void short_template_cache_workspace() {
    auto c=config(16,true);c.sample_rate=48000;c.bandwidth_hz=12000;c.carrier_hz=9000;
    c.dsss=true;c.dsss_seed[11]=139;
    modem::PatternSearch search;search.candidate_limit=32;search.track_limit=2;search.bit_limit=128;
    constexpr std::size_t small=192*1024,large=2*1024*1024;
    modem::PatternReceiver cached(c,large,search),uncached(c,small,search),shrinking(c,large,search);
    check(cached.working_bytes()>uncached.working_bytes()+100*1024,
          "short-template cache fixture must exercise cached and uncached execution");
    Bytes bits(96);std::mt19937_64 random(13);for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1U);
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto samples=waveform(c,bits,137,3*symbol,.73,.001);
    bool reduced=false;
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min<std::size_t>(137,samples.size()-offset);
        const auto block=std::span(samples).subspan(offset,count);
        cached.push(block);uncached.push(block);shrinking.push(block);offset+=count;
        if(!reduced && offset>samples.size()/3) {
            shrinking.set_workspace_bytes(small);reduced=true;
        }
        check(cached.working_bytes()<=large && uncached.working_bytes()<=small &&
              shrinking.working_bytes()<=(reduced?small:large),
              "optional template caches must remain inside their workspace limit");
    }
    cached.finish();uncached.finish();shrinking.finish();
    const auto expected=cached.take_bursts();
    check(expected.size()==1 && expected.front().bits==bits,"cached short templates lost payload bits");
    for(auto* receiver:{&uncached,&shrinking}) {
        const auto actual=receiver->take_bursts();
        check(actual.size()==1 && actual.front().bits==expected.front().bits &&
              actual.front().first_sample==expected.front().first_sample &&
              actual.front().end_sample==expected.front().end_sample &&
              actual.front().frequency_hz==expected.front().frequency_hz &&
              actual.front().score==expected.front().score &&
              actual.front().support_samples==expected.front().support_samples,
              "cache availability or eviction changed exact pattern decisions or confidence");
        const auto a=cached.candidates(),b=receiver->candidates();
        check(a.size()==b.size(),"cache availability changed retained search coverage");
        for(std::size_t i=0;i<a.size();++i)
            check(a[i].first_sample==b[i].first_sample && a[i].stream_symbol==b[i].stream_symbol &&
                  a[i].frequency_hz==b[i].frequency_hz && a[i].score==b[i].score &&
                  a[i].alternative_score==b[i].alternative_score && a[i].bit==b[i].bit,
                  "cache availability changed a timing/key/frequency/label hypothesis");
    }
    // Keep the cache while reducing spare RAM, then grow a payload
    // to the complete configured bit limit. Bit allocations must evict the
    // cache and recreate ordinary template rows without shrinking that limit.
    search.bit_limit=4096;
    modem::PatternReceiver pressure(c,large,search);
    const auto cached_bytes=pressure.working_bytes();constexpr std::size_t pressure_budget=320*1024;
    pressure.set_workspace_bytes(pressure_budget);
    check(pressure.working_bytes()==cached_bytes,"a fitting optional cache should survive workspace reduction");
    modem::PatternReceiver reference(c,pressure_budget,search);
    Bytes full(search.bit_limit);for(auto& bit:full)bit=static_cast<std::uint8_t>(random()&1U);
    const auto longer=waveform(c,full,137,3*symbol,.73,.001);
    Bytes grown_bits,ordinary_bits;
    const auto drain_bits=[](auto& receiver,Bytes& output) {for(auto& event:receiver.take_bursts())output.insert(output.end(),event.bits.begin(),event.bits.end());};
    for(std::size_t offset=0;offset<longer.size();) {
        const auto count=std::min<std::size_t>(997,longer.size()-offset);
        const auto block=std::span(longer).subspan(offset,count);
        pressure.push(block);reference.push(block);offset+=count;
        drain_bits(pressure,grown_bits);drain_bits(reference,ordinary_bits);
        check(pressure.working_bytes()<=pressure_budget,"payload growth exceeded workspace before cache eviction");
    }
    pressure.finish();reference.finish();
    drain_bits(pressure,grown_bits);drain_bits(reference,ordinary_bits);
    check(grown_bits==full && ordinary_bits==full,"draining payload chunks changed cached or uncached decisions");
    check(pressure.working_bytes()<=pressure_budget,"chunked payload retention exceeded workspace");
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
        const auto prefix=static_cast<std::size_t>(modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c));
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
int main(int argc,char** argv) {
    unsigned failures=0;
    const auto run=[&](const char* name,auto test) {
        if(argc>1 && std::string(name).find(argv[1])==std::string::npos)return;
        try { test();std::cout<<name<<": passed\n"; }
        catch(const std::exception& error){++failures;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("exact blind bits",exact_blind_bits);run("chunk invariance and late start",changing_chunks_and_late_start);
    run("carrier evidence after distorted startup",carrier_evidence_recovers_after_distorted_start);
    run("short pattern sample timing",short_pattern_sample_timing);
    run("short pattern wrong keys and noise",short_pattern_wrong_key_and_noise);
    run("high-bandwidth short public and private patterns",high_bandwidth_short_patterns);
    run("short private noise evidence",short_private_noise_evidence);
    run("private template energy normalization",private_template_energy_normalization);
    run("weak prefix confidence",weak_prefix_cannot_borrow_payload_confidence);
    run("pending tail joint confidence",pending_tail_requires_joint_confidence);
    run("unconfirmed tail and later start",unconfirmed_tail_cannot_veto_later_start);
    run("timed gap and independent start",timed_gap_cannot_veto_independent_start);
    run("noise-hidden chip observations",noise_hidden_chips);run("wrong keys and finite noise captures",wrong_key_and_background);
    run("multiple bursts",multiple_bursts);run("memory limits and cancellation",bounds_and_cancellation);
    run("fractional symbol timing",fractional_symbol_timing);run("keyed capture missing first symbol",keyed_capture_missing_first_symbol);
    run("private tracking across missing symbols",keyed_track_survives_missing_symbols);
    run("default gap timeout ends active message",default_gap_timeout_ends_active_message);
    run("independent epoch phase acquisition",independently_started_epoch_recovers_phase);
    run("independent sampled crystal and phase",independent_sampled_channel);
    run("high-SNR sampled private and public patterns",high_snr_sampled_channel);
    run("orthogonal private pattern bins",orthogonal_private_pattern_bins);
    run("shared projection and workspace updates",shared_projection_and_workspace_update);
    run("short pattern shared projection phase",short_pattern_shared_projection_phase);
    run("short template cache workspace and exact equivalence",short_template_cache_workspace);
    run("parallel search exact progress",parallel_search_exact_progress);
    run("parallel search physical absence",parallel_search_physical_absence);
    run("bounded long clock-window fallback",long_clock_window_fallback);
    run("hardware settling remains outside payload",hardware_settling_is_not_payload);
    return failures?1:0;
}
