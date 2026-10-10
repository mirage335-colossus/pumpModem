#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/channel.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/pattern_fft_batch.hpp"
#include "../src/pattern_fft_window.hpp"
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
void check(bool condition,const std::string& message) { if(!condition)throw Error(message); }
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
                    a.bit,a.stream_phase_samples,a.admission_threshold,a.frequency_hypothesis,a.outer_presence_score,a.outer_presence_active)==
           std::tie(b.first_sample,b.end_sample,b.stream_symbol,b.frequency_hz,b.score,b.alternative_score,
                    b.bit,b.stream_phase_samples,b.admission_threshold,b.frequency_hypothesis,b.outer_presence_score,b.outer_presence_active);
}
struct ComparedProgress {
    Bytes bits;
    std::size_t pending_polls=0,complete_events=0,candidates=0;
    std::size_t completed_at=0;
};
ComparedProgress compare_parallel_progress(const std::vector<float>& samples,const modem::Config& c,
                                          modem::PatternSearch search,unsigned workers,bool shrink=false,
                                          bool finish_capture=true) {
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
    if(finish_capture) {
        serial.finish();parallel.finish();poll(samples.size());
        serial.finish();parallel.finish();poll(samples.size());
    }
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
void outer_dsss_progress_and_absence() {
    auto c=config(64,true);c.bandwidth_hz=100;c.dsss_factor=10;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(i*13+29);
    const Bytes bits{0,0,1};
    const auto symbol=modem::symbol_sample_count(c),absence=modem::pattern_absence_samples(c);
    modem::PatternSearch search;search.frequency_offsets_hz={0};
    search.compact_clock_search=true;search.start_offset_seconds=137./c.sample_rate;search.start_uncertainty_seconds=.01;
    const auto samples=waveform(c,bits,137,absence+2*symbol+503,.73,.05,671);
    const auto progress=compare_parallel_progress(samples,c,search,3);
    if(progress.bits!=bits || progress.pending_polls<bits.size() || progress.complete_events!=1) {
        std::string observed;for(auto bit:progress.bits)observed+=bit?'1':'0';
        throw Error("outer DSSS lost independent bit progress or physical completion: bits="+observed+
            " pending="+std::to_string(progress.pending_polls)+" complete="+std::to_string(progress.complete_events));
    }
    check(progress.completed_at>=137+bits.size()*symbol+absence,
        "outer DSSS completed before fully observed absent symbols");
    const auto prefix=waveform(c,bits,137,symbol/2,.73,.05,671);
    const auto incomplete=compare_parallel_progress(prefix,c,search,3);
    check(incomplete.bits==bits && incomplete.complete_events==0,"outer DSSS treated EOF as observed absence");
    constexpr std::array<std::size_t,3> chunks{19,503,71};
    c.dsss_seed[0]^=0x40;
    const auto wrong=receive(samples,c,chunks,search);
    check(std::any_of(wrong.candidates.begin(),wrong.candidates.end(),[](const auto& e) {
        return e.score>=e.admission_threshold && e.outer_presence_active && e.outer_presence_score<e.admission_threshold;
    }),"outer DSSS fixture did not exercise strong structured-code rejection");
    check(wrong.bursts.empty(),"outer DSSS accepted a different dedicated outer key");
}
void outer_dsss_weak_awgn_preserves_admission() {
    auto c=config(64,true);c.bandwidth_hz=100;c.dsss_factor=10;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(i*13+29);
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.worker_threads=1;
    search.start_offset_seconds=137./c.sample_rate;search.start_uncertainty_seconds=.001;
    constexpr std::array<std::size_t,3> chunks{19,503,71};
    for(const bool compact:{true,false})for(const double noise:{3.,5.}) {
        search.compact_clock_search=compact;search.outer_presence_guard=true;
        const auto samples=waveform(c,Bytes{0,1},137,modem::symbol_sample_count(c),.73,noise,671);
        const auto guarded=receive(samples,c,chunks,search);
        search.outer_presence_guard=false;
        const auto preceding=receive(samples,c,chunks,search);
        check(guarded.bursts.size()==preceding.bursts.size() &&
            std::equal(guarded.bursts.begin(),guarded.bursts.end(),preceding.bursts.begin(),same_burst),
            "outer guard changed weak AWGN decisions, endpoints or progress");
        for(const auto& e:guarded.candidates)check(!e.outer_presence_active,
            "weak AWGN fixture unexpectedly entered strong colored-excess scope");
    }
}
void long_timing_guide_preserves_bit_owner() {
    auto c=config(64,true);c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=2;
    const Bytes bits{0,0,1};const auto symbol=modem::symbol_sample_count(c);
    check(symbol==4096,"timing guide fixture changed its long-symbol geometry");
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.compact_clock_search=true;
    search.start_offset_seconds=8;search.start_uncertainty_seconds=1;
    search.drift_tolerant=false;search.chunk_bits=1;search.worker_threads=1;
    // Keep this boundary regression above threshold but outside the legacy
    // factor1 strongly colored wrong-pattern failure (sigma=.03 admits bit1
    // at origin448 with score169.62 before the true origin512 is complete).
    const auto samples=waveform(c,bits,512,2*symbol,.21,1.5,173);
    modem::PatternReceiver receiver(c,8*1024*1024,search);
    Bytes accepted;std::size_t completed=0;std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
    const auto end=512+(bits.size()+1)*symbol;
    for(std::size_t i=0;i<samples.size();++i) {
        receiver.push(std::span(samples).subspan(i,1));
        for(const auto& event:receiver.take_bursts()) {
            const auto current=std::pair(event.stream_first_sample,event.stream_first_symbol);
            if(!identity)identity=current;
            check(identity==current,"timing guide replaced immutable accepted-bit ownership");
            accepted.insert(accepted.end(),event.bits.begin(),event.bits.end());
            check(accepted.size()<=bits.size() && std::equal(accepted.begin(),accepted.end(),bits.begin()),
                "timing guide changed a pending canonical bit prefix at sample "+std::to_string(i+1)+
                " origin "+std::to_string(event.stream_first_sample)+" score "+std::to_string(event.score)+
                " bits "+[&]{std::string text;for(auto b:accepted)text+=b?'1':'0';return text;}());
            if(event.complete){check(i+1>=end,"timing guide completed before compatible later absence");++completed;}
        }
        if(i+1==end-1)check(completed==0,"timing guide manufactured the final absent sample");
        if(i+1==end)check(completed==1,"timing guide delayed completed absence by another symbol");
    }
    receiver.finish();check(accepted==bits && completed==1,"timing guide lost payload or unique physical end");
}
void qualified_clock_window() {
    constexpr std::size_t workspace=8*1024*1024;
    // Fixed phase need not lie on the phase-search gcd lattice. In particular
    // phase7 must never be rounded to0 by a qualified window.
    for(const unsigned chips:{15U,75U})for(const unsigned phase:{0U,7U}) {
        auto c=config(chips,true);c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=64;
        c.stream_phase_samples=phase;
        modem::PatternSearch search;search.frequency_offsets_hz={0};search.compact_clock_search=true;
        search.start_offset_seconds=0;search.start_uncertainty_seconds=1;search.worker_threads=1;
        const auto origin=18.L; // original half-chip coverage-cell boundary
        search.qualified_start_window=modem::PatternStartWindow{origin-1.01L*phase,1.01L,.025L};
        modem::PatternCorrelator receiver(c,search,workspace);
        check(receiver.work().hypotheses==2,"sub-chip UTC window lost an original neighboring cell or fixed phase");
        const auto symbol=modem::symbol_sample_count(c);
        std::vector<float> silence(symbol+20);
        receiver.push(std::span(silence).first(symbol));
        check(!receiver.initial_search_complete(),"UTC clock window retired before its complete symbol");
        receiver.push(std::span(silence).subspan(symbol));
        check(receiver.initial_search_complete(),"UTC clock window never completed its finite coverage");
    }
    // Multiple canonical phases across both shorter and longer than one-second
    // symbols. Pruning preserves half-chip coverage without changing PCM.
    for(const unsigned chips:{15U,75U})for(const unsigned phase:{0U,8U,112U}) {
        auto tx=config(chips,true);tx.sample_rate=256;tx.carrier_hz=64;tx.bandwidth_hz=64;
        tx.stream_phase_samples=phase;
        const Bytes bits{0,0,1};
        const auto samples=waveform(tx,bits,17,modem::pattern_absence_samples(tx)+512,.31,.04,738);
        auto rx=tx;rx.stream_phase_samples=0;
        modem::PatternSearch search;search.frequency_offsets_hz={0};search.compact_clock_search=true;
        search.search_stream_phases=true;search.start_offset_seconds=0;search.start_uncertainty_seconds=1;
        search.qualified_start_window=modem::PatternStartWindow{17.L-1.01L*phase,1.01L,.025L};
        const auto progress=compare_parallel_progress(samples,rx,search,3);
        check(progress.bits==bits && progress.complete_events==1,"qualified UTC phase map lost sampled payload/absence");
    }
    // More than sixteen seconds selects pulse projection. The phase lattice
    // has large holes after pruning: those holes must not renumber pulse
    // parity, canonical addresses, or paired frequency/clock alternatives.
    for(const auto origin:{-.137L,17.125L})for(const auto ppm:{-200.,200.}) {
        auto tx=config(64,true);tx.sample_rate=256;tx.carrier_hz=64;tx.bandwidth_hz=64;
        tx.integration_seconds=(4256.-.25)/256;tx.stream_phase_samples=224;
        const auto symbol=modem::symbol_sample_count(tx);
        check(symbol==4256 && modem::pattern_chip_samples(tx)==8,"qualified pulse fixture geometry changed");
        const auto rate=1+static_cast<long double>(ppm)*1e-6L;
        std::vector<float> samples(static_cast<std::size_t>(std::ceil(origin+2*symbol/rate))+257);
        modem::PatternCode pattern(tx,tx.stream_epoch);
        std::mt19937 random(409);std::normal_distribution<float> noise(0,.08F);
        for(std::size_t n=0;n<samples.size();++n) {
            samples[n]=noise(random);
            const auto within=(static_cast<long double>(n)-origin)*rate;
            if(within>=0 && within<symbol)samples[n]+=static_cast<float>((
                std::polar(1.,2*std::numbers::pi*(64.03125)*n/256+.71)*
                pattern.shaped_value(0,1,static_cast<double>(within))).real());
        }
        auto rx=tx;rx.stream_phase_samples=0;
        modem::PatternSearch search;search.compact_clock_search=true;search.search_stream_phases=true;
        search.start_offset_seconds=-1;search.start_uncertainty_seconds=2;
        search.hypotheses={{.03125,ppm},{-.03125,-ppm}};search.worker_threads=1;
        search.retain_score=0;search.candidate_limit=32;
        search.qualified_start_window=modem::PatternStartWindow{origin-1.01L*224,1.01L,.025L};
        modem::PatternCorrelator projected(rx,search,workspace),raw(rx,search,workspace,{true});
        check(projected.work().backend==modem::PatternCorrelationBackend::pulse &&
            raw.work().backend==modem::PatternCorrelationBackend::raw,
            "qualified UTC pulse regression did not exercise both scoring paths");
        check(projected.work().hypotheses==raw.work().hypotheses && projected.work().hypotheses>2,
            "qualified UTC backend changed supplied search lanes");
        std::size_t accepted=0;
        for(std::size_t n=0;n<samples.size();) {
            const auto count=std::min<std::size_t>(137,samples.size()-n);
            projected.push(std::span(samples).subspan(n,count));raw.push(std::span(samples).subspan(n,count));n+=count;
            check(projected.initial_search_complete()==raw.initial_search_complete(),
                "qualified projection changed complete-symbol coverage");
            const auto a=projected.candidates(),b=raw.candidates();
            check(a.size()==b.size(),"qualified projection changed candidate publication");
            for(std::size_t i=0;i<a.size();++i) {
                auto x=a[i],y=b[i];
                check(std::abs(x.score-y.score)<2e-6*std::max(1.,y.score) &&
                    std::abs(x.alternative_score-y.alternative_score)<2e-6*std::max(1.,y.alternative_score),
                    "qualified projection changed raw statistical evidence: origin="+std::to_string(double(origin))+
                    " ppm="+std::to_string(ppm)+" input="+std::to_string(n)+" first="+std::to_string(x.first_sample)+
                    " phase="+std::to_string(x.stream_phase_samples)+" score="+std::to_string(x.score)+
                    " raw="+std::to_string(y.score)+" alternative="+std::to_string(x.alternative_score)+
                    " raw_alternative="+std::to_string(y.alternative_score));
                x.score=y.score;x.alternative_score=y.alternative_score;
                check(same_evidence(x,y),"qualified projection changed candidate address, boundary or threshold");
            }
            const auto events=projected.take_bursts(),reference=raw.take_bursts();
            check(events.size()==reference.size(),"qualified projection delayed an accepted bit");
            for(std::size_t i=0;i<events.size();++i) {
                auto x=events[i],y=reference[i];x.score=y.score;
                check(same_burst(x,y),"qualified projection changed pending identity or physical completion");
                accepted+=x.bits.size();
            }
        }
        check(accepted>0,"qualified pulse map did not admit the sampled candidate");
    }
    auto c=config(64,true);modem::PatternSearch search;search.frequency_offsets_hz={0};
    search.compact_clock_search=true;search.start_offset_seconds=0;search.start_uncertainty_seconds=1;
    search.qualified_start_window=modem::PatternStartWindow{0,0,0};
    rejects([&]{modem::PatternCorrelator bad(c,search,workspace);},"zero UTC map scale accepted");
}
void qualified_fft_window_geometry() {
    using modem::detail::pattern_fft_batch_may_intersect;
    using modem::detail::pattern_fft_select_starts;
    // Independent enumeration of original integer-bin cells, including
    // fractional centers and both ends of the validated clock domain. Any
    // covered phase/rate/cell must retain its ENTIRE original FFT batch.
    for(const auto origin:{-.137L,17.125L,65.5L})for(const auto scale:{.99L,1.01L})
        for(const auto bin:{1U,4U,8U})for(const auto symbol:{125ULL,500ULL}) {
            const modem::PatternStartWindow prior{origin,scale,.025L};
            for(std::uint64_t index=0;index<4;++index)for(std::uint64_t first=0;first<2200;first+=37) {
                bool covered=false;
                for(const auto phase:{7ULL,15ULL,23ULL})for(const auto rate:{.99L,1.L,1.01L})
                    for(std::uint64_t start=first;start<first+19;++start) {
                        const auto expected=origin+scale*phase+index*static_cast<long double>(symbol)/rate;
                        if(std::abs(start*static_cast<long double>(bin)-expected)<=.025L+2.5L*bin)covered=true;
                    }
                if(covered)check(pattern_fft_batch_may_intersect(prior,symbol,index,7,23,first,19,bin),
                    "qualified FFT pruning excluded an original fractional phase/rate coverage cell");
                const auto selected=pattern_fft_select_starts(prior,symbol,index,7,23,8,first,19,bin);
                for(const auto phase:{7ULL,15ULL,23ULL})for(const auto rate:{.99L,1.L,1.01L})
                    for(std::size_t start=0;start<19;++start) {
                        const auto expected=origin+scale*phase+index*static_cast<long double>(symbol)/rate;
                        if(std::abs((first+start)*static_cast<long double>(bin)-expected)>.025L+2.5L*bin)continue;
                        check(selected.full||std::any_of(selected.ranges.begin(),selected.ranges.begin()+selected.range_count,
                            [&](const auto range){return start>=range.first&&start-range.first<range.count;}),
                            "restricted FFT work lost an original fractional phase/rate boundary cell");
                    }
            }
            check(!pattern_fft_batch_may_intersect(prior,symbol,3,7,23,100000,19,bin),
                "qualified FFT geometry failed to exclude a provably disjoint batch");
        }
    const modem::PatternStartWindow boundary{10.L,1,.125L};
    check(pattern_fft_batch_may_intersect(boundary,64,0,0,0,0,1,4),
        "qualified FFT pruning lost the two-bin refinement/half-cell boundary");
    check(!pattern_fft_batch_may_intersect(boundary,64,0,0,0,100,1,4),
        "qualified FFT pruning kept a separated coverage cell");
    check(pattern_fft_batch_may_intersect({0,0,0},64,0,0,0,100,1,4),
        "invalid FFT prior authorized work removal");
    check(pattern_fft_batch_may_intersect({1e16L,1,0},64,0,0,0,100,1,4),
        "imprecise FFT prior authorized work removal");
    const auto separated=pattern_fft_select_starts({0,1,0},64,0,0,256,128,0,300,1);
    check(!separated.full&&separated.range_count==3&&separated.selected_count==13&&
        separated.ranges[0].first==0&&separated.ranges[0].count==3&&
        separated.ranges[1].first==126&&separated.ranges[1].count==5&&
        separated.ranges[2].first==254&&separated.ranges[2].count==5,
        "restricted FFT merged disjoint canonical phase windows into a broad envelope");
    const auto empty=pattern_fft_select_starts({0,1,0},64,0,0,256,128,10000,300,1);
    check(!empty.full&&empty.selected_count==0&&empty.range_count==0,
        "restricted FFT retained a separated private template job");
    check(pattern_fft_select_starts({0,0,0},64,0,0,0,1,100,19,4).full&&
        pattern_fft_select_starts({1e16L,1,0},64,0,0,0,1,100,19,4).full&&
        pattern_fft_select_starts({0,1,0},64,0,0,65000,1000,0,65536,1).full,
        "restricted FFT failed to retain original work for uncertain or over-capacity range unions");
    // An authoritative paired bank supplies its physical rate extrema. Keep
    // every original lane/refinement cell while avoiding an invented +/-1%
    // symbol displacement for a +/-200ppm oscillator bank.
    const modem::PatternStartWindow tight{-.137L,1.0001L,.025L};
    constexpr double minimum_rate=1-200e-6,maximum_rate=1+200e-6;
    const auto narrow=pattern_fft_select_starts(tight,512000,3,0,32000,8000,380000,40000,4,
        minimum_rate,maximum_rate);
    const auto broad=pattern_fft_select_starts(tight,512000,3,0,32000,8000,380000,40000,4);
    check(!narrow.full&&narrow.selected_count<broad.selected_count,
        "qualified FFT work retained an invented clock domain instead of the authoritative bank bounds");
    for(const auto phase:{0ULL,8000ULL,16000ULL,24000ULL,32000ULL})
        for(const auto rate:{minimum_rate,1.,maximum_rate})for(std::size_t j=0;j<40000;++j) {
            const auto expected=tight.epoch_origin_samples+tight.phase_scale*phase+3.L*512000/rate;
            if(std::abs((380000.L+j)*4-expected)>tight.half_width_samples+10)continue;
            check(std::any_of(narrow.ranges.begin(),narrow.ranges.begin()+narrow.range_count,[&](const auto range) {
                return j>=range.first&&j-range.first<range.count;
            }),"qualified FFT authoritative clock bounds lost a fractional endpoint/refinement cell");
        }
    check(pattern_fft_select_starts(tight,64,0,0,0,1,0,19,4,0,1).full&&
        pattern_fft_select_starts(tight,64,0,0,0,1,0,19,4,1.001L,.999L).full,
        "invalid authoritative clock bounds authorized restricted work");
}
void qualified_fft_window_preserves_progress() {
    constexpr std::size_t workspace=4*1024*1024;
    const Bytes bits{0,0,1};
    for(const auto factor:{1U,10U})for(const bool phase_edge:{false,true}) {
        auto tx=config(64,true);tx.sample_rate=256*factor;tx.carrier_hz=tx.sample_rate/4.;
        tx.bandwidth_hz=64;tx.dsss_factor=factor;
        tx.integration_seconds=(500.L*factor-.25L)/tx.sample_rate;
        for(std::size_t i=0;i<tx.dsss_seed.size();++i)tx.dsss_seed[i]=static_cast<std::uint8_t>(29+13*i);
        const auto symbol=modem::symbol_sample_count(tx);
        const auto step=std::gcd(symbol,static_cast<std::uint64_t>(tx.sample_rate));
        const auto maximum_phase=(std::min(symbol,static_cast<std::uint64_t>(tx.sample_rate))-1)/step*step;
        tx.stream_phase_samples=phase_edge?maximum_phase:0;
        const auto ppm=phase_edge?200.:-200.,frequency=phase_edge?.03125:-.03125;
        modem::ChannelConfig impairment;impairment.snr_db=35;impairment.seed=671+factor+phase_edge;
        impairment.clock_error_ppm=ppm;impairment.frequency_offset_hz=frequency-tx.carrier_hz*ppm*1e-6;
        impairment.phase_noise_degrees_per_sqrt_second=.05;
        modem::SampledSimulationChannel channel(tx,impairment);
        modem::StreamingTransmitter source(modem::RawBits{bits},tx,workspace);
        const auto rate=1+ppm*1e-6L;
        const auto origin=channel.startup_offset_samples()+
            (modem::training_sample_count(tx)+static_cast<long double>(modem::pattern_pulse_padding_samples(tx)))/rate;
        check(origin!=std::floor(origin),"qualified FFT fixture lost its fractional sampled start");
        std::vector<float> samples;std::array<float,503> buffer{};
        while(const auto count=channel.read(source,buffer))samples.insert(samples.end(),buffer.begin(),buffer.begin()+count);
        const auto eof_samples=samples.size();
        auto trailing=modem::pattern_absence_samples(tx)+2*symbol+503;
        while(trailing) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,buffer.size()));
            channel.read_noise(std::span(buffer).first(count));samples.insert(samples.end(),buffer.begin(),buffer.begin()+count);
            trailing-=count;
        }
        auto rx=tx;rx.stream_phase_samples=0;
        modem::PatternSearch search;search.hypotheses={{-.03125,-200},{.03125,200}};
        search.frequency_rate_competition=true;
        search.search_stream_phases=true;search.start_offset_seconds=origin/tx.sample_rate;search.start_uncertainty_seconds=6;
        search.candidate_limit=256;search.track_limit=4;search.bit_limit=32;search.retain_score=30;
        search.early_qualified_fft=false; // Isolate selection arithmetic from the separately tested early schedule.
        search.restricted_direct_search=false; // Keep the original bitwise FFT score assertions; direct arithmetic has paired tolerance tests.
        search.partitioned_timing_search=false; // Partition arithmetic has its independent same-PCM score tests.
        search.worker_threads=phase_edge?3:1;
        modem::PatternReceiver full(rx,workspace,search);
        search.qualified_start_window=modem::PatternStartWindow{
            origin-static_cast<long double>(tx.stream_phase_samples)/rate,1/rate,.025L};
        search.restricted_timing_search=false;
        modem::PatternReceiver preceding(rx,workspace,search);
        search.restricted_timing_search=true;
        modem::PatternReceiver prior(rx,workspace,search);
        check(prior.fft_work().timing_window==modem::PatternFftWindowStatus::active,
            "eligible short FFT prior was not enforced");
        Bytes observed;std::size_t pending=0,complete=0;bool prefix_checked=false,evidence_checked=false;
        const auto poll=[&](std::size_t position) {
            const auto a=full.take_bursts(),b=prior.take_bursts(),old=preceding.take_bursts();
            check(a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),same_burst),
                "qualified FFT pruning changed next-poll bit publication or physical-end events");
            check(old.size()==b.size()&&std::equal(old.begin(),old.end(),b.begin(),same_burst),
                "restricted FFT work changed preceding next-poll events or immutable identity");
            for(const auto& event:b) {
                observed.insert(observed.end(),event.bits.begin(),event.bits.end());
                if(!event.complete&&!event.bits.empty())++pending;
                if(event.complete) {
                    ++complete;
                    check(position>=origin+bits.size()*symbol/rate+modem::pattern_absence_samples(tx),
                        "qualified FFT pruning manufactured early physical absence");
                }
            }
            check(full.fft_work().threshold_trials==prior.fft_work().threshold_trials,
                "qualified FFT pruning reduced the original alpha-spending trial count");
            check(preceding.fft_work().threshold_trials==prior.fft_work().threshold_trials,
                "restricted FFT work changed the preceding full trial penalty");
            const auto original=full.candidates(),retained=prior.candidates();
            for(const auto& candidate:retained)if(candidate.score>=candidate.admission_threshold) {
                evidence_checked=true;
                check(std::any_of(original.begin(),original.end(),[&](const auto& reference) {
                    return same_evidence(candidate,reference);
                }),"qualified FFT pruning changed an eligible score, energy, covariance or threshold");
            }
            check(full.working_bytes()<=workspace&&prior.working_bytes()<=workspace,
                "qualified FFT pruning exceeded the configured workspace");
        };
        constexpr std::array<std::size_t,5> chunks{1,137,19,503,71};
        for(std::size_t position=0,chunk=0;position<samples.size();++chunk) {
            auto count=std::min(chunks[chunk%chunks.size()],samples.size()-position);
            if(position<eof_samples)count=std::min(count,eof_samples-position);
            const auto block=std::span(samples).subspan(position,count);
            full.push(block);preceding.push(block);prior.push(block);position+=count;poll(position);
            if(position==eof_samples) {
                check(complete==0,"source EOF substituted for six seconds of observed absence");prefix_checked=true;
            }
        }
        full.finish();preceding.finish();prior.finish();poll(samples.size());
        check(prefix_checked&&evidence_checked&&observed==bits&&pending>0&&complete==1,
            "qualified FFT pruning lost exact short/DSSS bits or unique physical completion");
        const auto a=full.fft_work(),b=prior.fft_work();
        check(b.skipped_batches>0&&b.skipped_start_trials>0&&b.input_transforms<a.input_transforms&&
            b.template_jobs<a.template_jobs,
            "qualified FFT prior changed estimates without removing actual transforms and private jobs");
        const auto old=preceding.fft_work();
        check(b.restricted_timing_active&&!old.restricted_timing_active&&b.scored_start_trials==b.retained_start_trials&&
            b.scored_start_trials<old.scored_start_trials&&b.scored_start_trials+b.skipped_start_trials<=b.threshold_trials,
            "restricted FFT counters did not represent actual selected work and original trial coverage");
        std::stop_source stop;stop.request_stop();
        modem::PatternReceiver cancelled(rx,workspace,search);
        rejects([&]{cancelled.push(std::span(samples).first(1),stop.get_token());},
            "qualified FFT pruning ignored cancellation");
    }
    auto c=config(64,true);c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=64;
    modem::PatternSearch search;search.hypotheses={{0,0}};search.start_offset_seconds=0;search.start_uncertainty_seconds=6;
    search.worker_threads=1;
    std::vector<float> silence(12*c.sample_rate);
    modem::PatternReceiver full(c,workspace,search);
    search.qualified_start_window=modem::PatternStartWindow{0,1,.025L};
    modem::PatternReceiver prior(c,workspace,search);
    full.push(silence);prior.push(silence);full.finish();prior.finish();
    check(full.take_bursts().empty()&&prior.take_bursts().empty()&&
        full.fft_work().threshold_trials==prior.fft_work().threshold_trials&&prior.fft_work().skipped_batches>0,
        "qualified FFT noise-only pruning changed false-acceptance trial spending");
    for(const bool invalid:{false,true}) {
        auto unsupported=search;
        if(invalid)unsupported.qualified_start_window->phase_scale=0;
        else {unsupported.hypotheses.clear();unsupported.frequency_offsets_hz={0};}
        modem::PatternReceiver fallback(c,workspace,unsupported);
        check(fallback.fft_work().timing_window==(invalid?modem::PatternFftWindowStatus::invalid_numerics:
            modem::PatternFftWindowStatus::unsupported_geometry),
            "qualified FFT unsupported/numerical fallback was not reported");
        fallback.push(silence);fallback.finish();
        const auto original=full.fft_work(),unchanged=fallback.fft_work();
        check(unchanged.skipped_batches==0 && unchanged.input_transforms==original.input_transforms &&
            unchanged.template_jobs==original.template_jobs && unchanged.threshold_trials==original.threshold_trials &&
            fallback.take_bursts().empty(),
            "unsupported FFT geometry silently pruned required work");
    }
}
void qualified_fft_restricted_direct() {
    auto c=config(8,true);c.sample_rate=64;c.bandwidth_hz=8;c.carrier_hz=16;c.integration_seconds=8;
    modem::PatternSearch search;search.hypotheses={{0,-200},{0,200}};
    search.prefer_streamed_templates=true;search.start_offset_seconds=-17./64;
    search.start_uncertainty_seconds=6;search.qualified_start_window=modem::PatternStartWindow{-17,1,.025L};
    search.worker_threads=1;search.bit_limit=32;
    search.early_qualified_fft=false;
    constexpr std::size_t workspace=4*1024*1024;
    for(const auto workers:{1U,3U}) {
        search.worker_threads=workers;search.restricted_timing_search=false;search.release_idle_fft_scratch=false;
        modem::PatternReceiver preceding(c,workspace,search);
        search.restricted_timing_search=true;search.release_idle_fft_scratch=true;
        modem::PatternReceiver selected(c,workspace,search);
        const auto idle=selected.working_bytes();
        check(idle<preceding.working_bytes() && selected.reserved_workspace_bytes()>=preceding.working_bytes(),
            "idle epoch scratch must be released without reducing its active workspace reservation");
        std::vector<float> silence(48*c.sample_rate);
        for(std::size_t position=0;position<silence.size();) {
            const auto block=std::span(silence).subspan(position,std::min<std::size_t>(137,silence.size()-position));
            preceding.push(block);selected.push(block);position+=block.size();
            check(preceding.take_bursts().empty()&&selected.take_bursts().empty(),
                "restricted direct acquisition admitted noise or changed next-poll events");
            check(preceding.fft_work().threshold_trials==selected.fft_work().threshold_trials,
                "restricted direct acquisition changed original alpha spending");
            check(selected.working_bytes()==idle,
                "unacquired epochs retained scratch after a complete input call");
        }
        preceding.finish();selected.finish();
        const auto old=preceding.fft_work(),now=selected.fft_work();
        check(now.direct_jobs>0&&old.direct_jobs==0&&now.skipped_template_jobs>0&&
            now.scored_start_trials<old.scored_start_trials&&
            now.scored_start_trials+now.skipped_start_trials==now.threshold_trials&&
            now.threshold_trials==old.threshold_trials&&selected.working_bytes()<=workspace,
            "restricted direct path did not remove actual numerical work within its original workspace/trial bounds");
    }
}
void qualified_fft_partitioned_reception() {
    auto c=config(128,true);c.sample_rate=2560;c.bandwidth_hz=64;c.carrier_hz=640;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(17+7*i);
    c.integration_seconds=4;const auto symbol=modem::symbol_sample_count(c);
    const Bytes bits{0,1,0};constexpr std::size_t delay=1024,workspace=8*1024*1024;
    const auto pcm=waveform(c,bits,delay,12*c.sample_rate,.37,.001);
    modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=1;
    search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{delay,1,256};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    search.early_qualified_fft=false;search.component_qualified_search=false;
    search.partitioned_timing_search=false;search.restricted_direct_search=false;
    modem::PatternReceiver whole(c,workspace,search);
    search.partitioned_timing_search=true;search.restricted_direct_search=true;
    modem::PatternReceiver bounded(c,workspace,search);
    std::array<Bytes,2> recovered;std::array<unsigned,2> completed{};
    for(std::size_t position=0;position<pcm.size();) {
        const auto block=std::span(pcm).subspan(position,std::min<std::size_t>(317,pcm.size()-position));
        whole.push(block);bounded.push(block);position+=block.size();
        std::array<modem::PatternReceiver*,2> receivers{&whole,&bounded};
        for(std::size_t j=0;j<receivers.size();++j) {
            check(receivers[j]->working_bytes()<=workspace,"partitioned reception exceeded workspace");
            for(const auto& event:receivers[j]->take_bursts()) {
                recovered[j].insert(recovered[j].end(),event.bits.begin(),event.bits.end());
                if(event.complete) {++completed[j];check(position>=delay+bits.size()*symbol+modem::pattern_absence_samples(c),
                    "partitioned reception manufactured physical absence");}
            }
        }
    }
    check(recovered[0]==bits&&recovered[1]==bits&&completed[0]==1&&completed[1]==1,
        "partitioned reception changed exact independent private bits or physical completion");
    const auto a=whole.fft_work(),b=bounded.fft_work();
    check(b.partitioned_jobs>0&&b.partitioned_input_transforms>0&&a.partitioned_jobs==0&&
        b.input_transforms<a.input_transforms,"partitioned receiver did not eliminate original broad input transforms: jobs="+
        std::to_string(b.partitioned_jobs)+" direct="+std::to_string(b.direct_jobs)+" FFT="+std::to_string(b.fft_jobs)+
        " fallback="+std::to_string(b.partitioned_fallbacks)+" inputs="+std::to_string(b.input_transforms)+"/"+
        std::to_string(a.input_transforms)+" symbol="+std::to_string(symbol));
    check(a.scored_start_trials==b.scored_start_trials&&a.skipped_start_trials==b.skipped_start_trials,
        "partitioned reception changed admitted starts or acquisition trial accounting");
}
void search_arithmetic_preserves_retained_progress() {
    auto c=config(128,true);c.sample_rate=2560;c.bandwidth_hz=64;c.carrier_hz=640;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=4;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(91+3*i);
    const Bytes bits{0,1,0};constexpr std::size_t delay=1024,workspace=8*1024*1024;
    const auto pcm=waveform(c,bits,delay,12*c.sample_rate,.37,.003);
    modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=1;
    search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{delay,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    c.search_arithmetic=modem::SearchArithmetic::fp64;
    modem::PatternReceiver reference(c,workspace,search);
    c.search_arithmetic=modem::SearchArithmetic::int8;
    modem::PatternReceiver integer(c,workspace,search);
    Bytes received;unsigned completed=0;
    const auto poll=[&] {
        const auto a=reference.take_bursts(),b=integer.take_bursts();
        check(a.size()==b.size(),"INT8 screening changed next-poll publication count");
        for(std::size_t i=0;i<a.size();++i) {
            check(same_burst(a[i],b[i]),"INT8 screening changed retained evidence, pending identity or completion");
            received.insert(received.end(),b[i].bits.begin(),b[i].bits.end());completed+=b[i].complete;
        }
        const auto aw=reference.fft_work(),bw=integer.fft_work();
        check(aw.threshold_trials==bw.threshold_trials&&aw.scored_start_trials==bw.scored_start_trials,
              "search precision removed an admitted hypothesis or trial charge");
        check(integer.working_bytes()<=workspace,"integer screening exceeded the workspace bound");
    };
    for(std::size_t position=0;position<pcm.size();) {
        const auto block=std::span(pcm).subspan(position,std::min<std::size_t>(317,pcm.size()-position));
        reference.push(block);integer.push(block);position+=block.size();poll();
    }
    reference.finish();integer.finish();poll();
    check(received==bits&&completed==1,"integer search changed exact bits or observed absence");
    const auto work=integer.fft_work();
    check(work.int8_jobs>0&&work.int8_dots>0&&work.exact_refines>0,
          "explicit diagnostic search did not execute INT8 operands and original-statistic verification");
    check(reference.fft_work().int8_dots==0,"FP64 diagnostic reference quantized operands");
}
void native_fp32_whole_preserves_progress() {
    auto c=config(128,true);c.sample_rate=2560;c.bandwidth_hz=64;c.carrier_hz=640;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=4;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(91+3*i);
    const Bytes bits{0,1,0};constexpr std::size_t delay=1024,workspace=8*1024*1024;
    const auto original=waveform(c,bits,delay,12*c.sample_rate,.37,.003);
    for(const double scale:{1.,1e-30,1e30}) {
    auto pcm=original;for(auto& sample:pcm)sample=static_cast<float>(sample*scale);
    modem::PatternSearch search;search.hypotheses={{0,.0003}};search.drift_tolerant=false;search.initial_stream_symbols=1;
    search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{delay,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    search.partitioned_timing_search=false;search.restricted_direct_search=false;
    c.search_arithmetic=modem::SearchArithmetic::fp64;modem::PatternReceiver reference(c,workspace,search);
    c.search_arithmetic=modem::SearchArithmetic::fp32;modem::PatternReceiver candidate(c,workspace,search);
    Bytes received;unsigned completed=0;std::size_t observed=0;
    const auto poll=[&] {
        const auto a=reference.take_bursts();auto b=candidate.take_bursts();
        check(a.size()==b.size(),"native FP32 whole FFT delayed next-poll publication");
        for(std::size_t i=0;i<a.size();++i) {
            check(std::abs(a[i].score-b[i].score)<=2e-5*std::max(1.,std::abs(a[i].score)),
                "native FP32 whole FFT exceeded the numerical score tolerance");
            // Scores are allowed to round, identity and every physical field are not.
            b[i].score=a[i].score;
            check(same_burst(a[i],b[i]),"native FP32 changed pending identity, bits or physical support");
            check(b[i].end_sample<=observed,"native FP32 used unobserved samples");
            if(b[i].complete)check(observed>=delay+bits.size()*modem::symbol_sample_count(c)+modem::pattern_absence_samples(c),
                "native FP32 manufactured physical absence");
            received.insert(received.end(),b[i].bits.begin(),b[i].bits.end());completed+=b[i].complete;
        }
        const auto aw=reference.fft_work(),bw=candidate.fft_work();
        check(aw.threshold_trials==bw.threshold_trials&&aw.scored_start_trials==bw.scored_start_trials&&
            aw.retained_start_trials==bw.retained_start_trials,"native FP32 changed acquisition coverage");
        check(candidate.working_bytes()<=workspace,"native FP32 whole FFT exceeded workspace");
    };
    while(observed<pcm.size()) {
        const auto block=std::span(pcm).subspan(observed,std::min<std::size_t>(317,pcm.size()-observed));
        reference.push(block);candidate.push(block);observed+=block.size();poll();
    }
    reference.finish();candidate.finish();poll();
    reference.finish();candidate.finish();poll();
    const auto work=candidate.fft_work();
    check((scale==1.?work.native_fp32_transforms>0:work.native_fp32_transforms==0)&&
        work.fft_jobs>0&&work.int8_dots==0&&work.partitioned_jobs==0&&work.direct_jobs==0,
        "whole FP32 regression did not execute native float transforms");
    if(scale!=1.)check(work.arithmetic_fallback_jobs>0,"unsafe FP32 input did not report range fallback");
    check(scale!=1. || (received==bits&&completed==1),"native FP32 changed the received bits or completion count: scale="+std::to_string(scale)+" received="+std::to_string(received.size())+" completions="+std::to_string(completed));
    }
}
void automatic_workers_select_native_private_search() {
    auto c=config(64,true);c.sample_rate=40000;c.bandwidth_hz=10;c.carrier_hz=7500;
    c.dsss=true;c.dsss_factor=1000;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=0;c.search_arithmetic=modem::SearchArithmetic::default_mode;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(91+3*i);
    modem::PatternSearch search;search.worker_threads=0;search.drift_tolerant=false;
    search.hypotheses={{0,0}};search.initial_stream_symbols=2;
    search.start_offset_seconds=.1;search.start_uncertainty_seconds=7;
    search.qualified_start_window=modem::PatternStartWindow{4000,1,20};
    constexpr std::size_t workspace=64*1024*1024;
    modem::PatternReceiver automatic(c,workspace,search);search.worker_threads=1;
    modem::PatternReceiver serial(c,workspace,search);
    // A complete logical FFT window tests dispatch even when no signal admits.
    std::vector<float> pcm(1048576);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(.01*std::sin(i*.317));
    automatic.push(pcm);serial.push(pcm);
    const auto a=automatic.fft_work(),b=serial.fft_work();
    check(a.native_fp32_transforms+a.fp32_dots>0&&
        a.native_fp32_transforms==b.native_fp32_transforms&&a.fp32_dots==b.fp32_dots&&
        a.threshold_trials==b.threshold_trials&&a.scored_start_trials==b.scored_start_trials,
        "application automatic workers missed the supported native precision path: "+
        std::to_string(a.native_fp32_transforms)+"/"+std::to_string(b.native_fp32_transforms)+" transforms, "+
        std::to_string(a.fp32_dots)+"/"+std::to_string(b.fp32_dots)+" dots, symbol "+
        std::to_string(modem::symbol_sample_count(c)));
    const auto ap=automatic.take_bursts(),bp=serial.take_bursts();
    check(ap.size()==bp.size(),"automatic worker selection changed progress");
    for(std::size_t i=0;i<ap.size();++i)check(same_burst(ap[i],bp[i]),"automatic workers changed scored identity");
    check(automatic.working_bytes()<=workspace,"automatic native dispatch exceeded workspace");
    // Optional native scratch must fit before changing automatic scheduling.
    // Keep the whole-FFT algorithm here so a partitioned path cannot mask it.
    const auto tight=automatic.reserved_workspace_bytes()+1024*1024;
    search.worker_threads=0;search.partitioned_timing_search=false;search.restricted_direct_search=false;
    modem::PatternReceiver pressure(c,tight,search);pressure.push(pcm);
    const auto p=pressure.fft_work();
    check(p.native_fp32_transforms==0&&p.arithmetic_fallback_jobs==0&&p.fft_jobs>0&&
          pressure.working_bytes()<=tight,
          "automatic workers forced serial native execution without workspace for its buffers");
}
void native_fp32_private_template_progress() {
    auto c=config(128,true);c.sample_rate=2560;c.bandwidth_hz=64;c.carrier_hz=640;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=4;
    for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=static_cast<std::uint8_t>(91+3*i);
    const Bytes bits{0,1,0};constexpr std::size_t delay=1024,workspace=8*1024*1024;
    const auto original=waveform(c,bits,delay,12*c.sample_rate,.37,.003);
    for(const unsigned path:{0U,1U,2U})for(const double scale:{1.,1e-30,1e30,0.}) {
    const bool bounded=path==1,mixed=path==2;
    const bool small=scale==0.;
    auto pcm=original;for(auto& sample:pcm)sample=static_cast<float>(sample*(small?1.:scale));
    modem::PatternSearch search;search.hypotheses={{0,0},{.0003,.0006},{-.0003,-.0006}};search.drift_tolerant=false;search.initial_stream_symbols=1;
    search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{delay,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    search.partitioned_timing_search=bounded;search.restricted_direct_search=bounded||mixed;
    c.search_arithmetic=modem::SearchArithmetic::fp64;modem::PatternReceiver reference(c,workspace,search);
    c.search_arithmetic=modem::SearchArithmetic::fp32;modem::PatternReceiver candidate(c,workspace,search);
    c.search_arithmetic=modem::SearchArithmetic::default_mode;modem::PatternReceiver automatic(c,workspace,search);
    const auto allowance=small?std::max(reference.reserved_workspace_bytes(),candidate.reserved_workspace_bytes())+48*1024:workspace;
    if(small){reference.set_workspace_bytes(allowance);candidate.set_workspace_bytes(allowance);automatic.set_workspace_bytes(allowance);}
    Bytes received;unsigned completed=0;std::size_t observed=0;
    const auto poll=[&] {
        const auto a=reference.take_bursts();auto b=candidate.take_bursts();const auto d=automatic.take_bursts();
        check(b.size()==d.size(),"automatic policy delayed publication relative to FP32 minimum");
        for(std::size_t i=0;i<b.size();++i)check(same_burst(b[i],d[i]),"automatic policy changed resolved FP32 evidence or pending identity");
        check(a.size()==b.size(),"native FP32 whole FFT delayed next-poll publication");
        for(std::size_t i=0;i<a.size();++i) {
            check(std::abs(a[i].score-b[i].score)<=2e-5*std::max(1.,std::abs(a[i].score)),
                "private cache numerical score mismatch small="+std::to_string(small)+" bounded="+std::to_string(bounded)+" scale="+std::to_string(scale)+" old="+std::to_string(a[i].score)+" new="+std::to_string(b[i].score));
            // Scores are allowed to round, identity and every physical field are not.
            b[i].score=a[i].score;
            check(same_burst(a[i],b[i]),"native FP32 changed pending identity, bits or physical support");
            check(b[i].end_sample<=observed,"native FP32 used unobserved samples");
            if(b[i].complete)check(observed>=delay+bits.size()*modem::symbol_sample_count(c)+modem::pattern_absence_samples(c),
                "native FP32 manufactured physical absence");
            received.insert(received.end(),b[i].bits.begin(),b[i].bits.end());completed+=b[i].complete;
        }
        const auto aw=reference.fft_work(),bw=candidate.fft_work();
        check(aw.threshold_trials==bw.threshold_trials&&aw.scored_start_trials==bw.scored_start_trials&&
            aw.retained_start_trials==bw.retained_start_trials,"native FP32 changed acquisition coverage");
        check(automatic.working_bytes()<=allowance&&candidate.working_bytes()<=allowance,"native FP32 whole FFT exceeded workspace");
    };
    while(observed<pcm.size()) {
        const auto block=std::span(pcm).subspan(observed,std::min<std::size_t>(317,pcm.size()-observed));
        reference.push(block);candidate.push(block);automatic.push(block);observed+=block.size();poll();
    }
    reference.finish();candidate.finish();automatic.finish();poll();
    reference.finish();candidate.finish();automatic.finish();poll();
    const auto work=candidate.fft_work(),auto_work=automatic.fft_work();
    check(auto_work.int8_dots==0&&auto_work.int8_jobs==0&&auto_work.native_fp32_transforms==work.native_fp32_transforms&&
        auto_work.private_template_builds==work.private_template_builds&&auto_work.threshold_trials==work.threshold_trials&&
        auto_work.scored_start_trials==work.scored_start_trials,"automatic policy changed arithmetic, reuse or retained hypotheses");
    if(mixed&&scale==1.)check(auto_work.native_fp32_transforms==0&&auto_work.arithmetic_fallback_jobs>0,
        "mixed generic fallback executed unsupported wide-buffer FP32");
    if(scale==1.&&!mixed)check(work.private_template_builds>0&&work.private_template_lane_jobs>=3*work.private_template_builds&&
        work.private_template_peak_bytes>0&&work.int8_dots==0,"private acquisition cache was not exercised");
    else if(!small || bounded || mixed)check(work.private_template_builds==0,"fallback used approximate private cache");
    if(small && bounded)check(work.direct_jobs>0&&work.fp32_dots==0,
        "a small cache enabled direct arithmetic after its stack reservation was denied");
    check(reference.fft_work().private_template_builds==0,"FP64 control used approximate private cache");
    if(scale!=1. && !small)check(work.arithmetic_fallback_jobs>0,"unsafe FP32 input did not report range fallback");
    check((scale!=1. && !small) || (received==bits&&completed==1),"native FP32 changed the received bits or completion count: scale="+std::to_string(scale)+" received="+std::to_string(received.size())+" completions="+std::to_string(completed));
    }
}
void qualified_fft_bounded_readiness() {
    auto c=config(32,true);c.sample_rate=64;c.bandwidth_hz=8;c.carrier_hz=16;
    c.integration_seconds=8;c.pulse_shaping=false;
    const Bytes bits{0,1,0};const auto symbol=modem::symbol_sample_count(c);
    const auto pcm=waveform(c,bits,8,4*symbol,.37,.001);
    modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=1;
    search.start_offset_seconds=8./64;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{8,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    search.early_qualified_fft=false;modem::PatternReceiver broad(c,4*1024*1024,search);
    search.early_qualified_fft=true;modem::PatternReceiver bounded(c,4*1024*1024,search);
    std::array<Bytes,2> recovered;std::array<std::size_t,2> first{},completed{};
    for(std::size_t position=0;position<pcm.size();) {
        const auto block=std::span(pcm).subspan(position,std::min<std::size_t>(13,pcm.size()-position));
        broad.push(block);bounded.push(block);position+=block.size();
        std::array<modem::PatternReceiver*,2> receivers{&broad,&bounded};
        for(std::size_t i=0;i<receivers.size();++i)for(const auto& event:receivers[i]->take_bursts()) {
            if(!event.bits.empty() && !first[i])first[i]=position;
            recovered[i].insert(recovered[i].end(),event.bits.begin(),event.bits.end());
            if(event.complete) {
                ++completed[i];
                check(position>=8+bits.size()*symbol+modem::pattern_absence_samples(c),
                    "early qualified scheduling manufactured absent physical samples");
            }
        }
    }
    check(recovered[0]==bits && recovered[1]==bits && completed[0]==1 && completed[1]==1,
        "bounded FFT readiness changed exact bits or physical completion");
    check(first[1]>=8+symbol && first[1]+2*c.sample_rate<first[0] && bounded.fft_work().early_batches>0,
        "qualified acquisition still waited for the original broad FFT hop");
    modem::PatternReceiver partial(c,4*1024*1024,search);
    partial.push(std::span(pcm).first(symbol/2));partial.finish();
    check(partial.take_bursts().empty(),"EOF supplied the rest of an early qualified symbol");
}
void qualified_fft_followon_progress() {
    auto c=config(32,true);c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;
    c.integration_seconds=260./256;c.pulse_shaping=false;
    const Bytes bits{0,1,0};const auto symbol=modem::symbol_sample_count(c);
    check(symbol==260,"follow-on fixture lost its partial-chip geometry");
    const auto pcm=waveform(c,bits,8,8*c.sample_rate,.37,.001);
    modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=1;
    search.start_offset_seconds=8./256;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{8,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    modem::PatternReceiver receiver(c,4*1024*1024,search);Bytes recovered;unsigned completed=0;
    for(std::size_t position=0;position<pcm.size();) {
        const auto block=std::span(pcm).subspan(position,std::min<std::size_t>(11,pcm.size()-position));
        receiver.push(block);position+=block.size();
        for(const auto& event:receiver.take_bursts()) {
            recovered.insert(recovered.end(),event.bits.begin(),event.bits.end());
            if(event.complete) {
                ++completed;check(position>=8+bits.size()*symbol+modem::pattern_absence_samples(c),
                    "bounded continuation manufactured absent physical samples");
            }
        }
        for(std::size_t k=1;k<=bits.size();++k)
            if(position>=8+k*symbol+32)check(recovered.size()>=k,
                "fully observed follow-on bit waited behind an unrelated acquisition hop");
        if(position>=8+bits.size()*symbol+modem::pattern_absence_samples(c)+32)
            check(completed==1,"observed physical absence waited behind an unrelated acquisition hop");
    }
    check(recovered==bits&&completed==1,"bounded continuation changed exact bits or physical completion");
}
void qualified_fft_component_progress() {
    auto c=config(32,true);c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;
    c.integration_seconds=260./256;c.pulse_shaping=false;
    const Bytes bits{0,1,0};const auto symbol=modem::symbol_sample_count(c);
    const auto pcm=waveform(c,bits,8,10*c.sample_rate,.37,.001);
    modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=3;
    search.start_offset_seconds=8./256;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{8,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    search.component_qualified_search=false;modem::PatternReceiver whole(c,4*1024*1024,search);
    search.component_qualified_search=true;modem::PatternReceiver split(c,4*1024*1024,search);
    std::array<Bytes,2> recovered;std::array<std::size_t,2> first{},completed{};
    for(std::size_t position=0;position<pcm.size();) {
        const auto block=std::span(pcm).subspan(position,std::min<std::size_t>(11,pcm.size()-position));
        whole.push(block);split.push(block);position+=block.size();
        std::array<modem::PatternReceiver*,2> receivers{&whole,&split};
        for(std::size_t i=0;i<receivers.size();++i)for(const auto& event:receivers[i]->take_bursts()) {
            if(!event.bits.empty()&&!first[i])first[i]=position;
            recovered[i].insert(recovered[i].end(),event.bits.begin(),event.bits.end());
            if(event.complete) {
                ++completed[i];check(position>=8+bits.size()*symbol+modem::pattern_absence_samples(c),
                    "component scheduling manufactured physical absence");
            }
        }
        for(std::size_t k=1;k<=bits.size();++k)if(position>=8+k*symbol+32)
            check(recovered[1].size()>=k,"independent timing component waited for a distant initial-symbol alternative");
    }
    check(recovered[0]==bits&&recovered[1]==bits&&completed[0]==1&&completed[1]==1,
        "component schedule changed exact bits or completion count");
    check(first[1]+symbol<first[0]&&split.fft_work().timing_components>=3,
        "component schedule did not reduce measured first-bit media latency");
    const auto a=whole.fft_work(),b=split.fft_work();
    check(a.scored_start_trials+a.skipped_start_trials==b.scored_start_trials+b.skipped_start_trials,
        "component schedule changed the full acquisition trial charge");
    modem::PatternReceiver partial(c,4*1024*1024,search);
    partial.push(std::span(pcm).first(symbol/2));partial.finish();
    check(partial.take_bursts().empty(),"component EOF supplied unobserved symbol data");
    search.candidate_limit=1;modem::PatternReceiver limited(c,4*1024*1024,search);
    limited.push(std::span(pcm).first(2*symbol));
    check(limited.fft_work().timing_components==0&&limited.fft_work().component_fallbacks>0,
        "component split bypassed the original logical-hop candidate capacity");
}
void qualified_fft_partial_eof_threshold() {
    auto c=config(32,true);c.sample_rate=256;c.bandwidth_hz=64;c.carrier_hz=64;
    c.integration_seconds=260./256;c.pulse_shaping=false;
    const auto clean=waveform(c,Bytes{0},8,32,.37);
    check(clean.size()==300,"partial EOF fixture changed physical extent");
    std::mt19937_64 random(173);std::normal_distribution<double> gaussian(0,.2);
    std::vector<float> noise(clean.size());for(auto& value:noise)value=static_cast<float>(gaussian(random));
    modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=3;
    search.start_offset_seconds=8./256;search.start_uncertainty_seconds=6;
    search.qualified_start_window=modem::PatternStartWindow{8,1,.025L};
    search.prefer_streamed_templates=true;search.worker_threads=1;search.bit_limit=32;
    struct Result {Bytes bits;double score=0,threshold=0;std::uint64_t trials=0,replays=0;};
    const auto run=[&](double gain,bool component,std::size_t extent=300,bool cancel_finish=false,unsigned alternatives=3) {
        auto local=search;local.component_qualified_search=component;
        local.early_qualified_fft=component;local.initial_stream_symbols=alternatives;
        modem::PatternReceiver receiver(c,4*1024*1024,local);Result out;
        std::vector<float> pcm(extent);
        for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(noise[i]+gain*clean[i]);
        const auto drain=[&] {
            for(const auto& event:receiver.take_bursts()) {
                check(!event.complete,"partial EOF manufactured observed absence");
                out.bits.insert(out.bits.end(),event.bits.begin(),event.bits.end());
            }
        };
        for(std::size_t i=0;i<pcm.size();i+=11) {
            receiver.push(std::span(pcm).subspan(i,std::min<std::size_t>(11,pcm.size()-i)));drain();
        }
        if(cancel_finish) {
            std::stop_source canceled;canceled.request_stop();
            rejects([&]{receiver.finish(canceled.get_token());},"canceled finish performed EOF replay");
            check(receiver.fft_work().eof_replays==0,"canceled finish changed replay state");
        }
        receiver.finish();drain();receiver.finish();
        check(receiver.take_bursts().empty(),"repeated partial EOF emitted duplicate progress");
        for(const auto& item:receiver.candidates())if(item.score>out.score) {
            out.score=item.score;out.threshold=item.admission_threshold;
        }
        const auto work=receiver.fft_work();out.trials=work.threshold_trials;out.replays=work.eof_replays;
        return out;
    };
    // One fixed noise realization. Tune only signal amplitude to specified
    // statistic levels; this is a deterministic threshold regression, not a
    // detection-probability or sensitivity experiment.
    for(const unsigned alternatives:{1U,3U}) {
    const auto reference=run(1,false,300,false,alternatives);const auto old_threshold=reference.threshold;
    const auto live_threshold=-std::log(search.false_alarm_probability)+2*std::log(192.*alternatives+1)+std::log(6.);
    check(live_threshold>old_threshold+1,"EOF regression lost its unequal final/live trial geometry");
    for(unsigned level=0;level<3;++level) {
        const auto target=level==0?old_threshold-.25:level==1?(old_threshold+live_threshold)/2:live_threshold+.25;
        double low=0,high=4;
        check(run(low,false,300,false,alternatives).score<target&&run(high,false,300,false,alternatives).score>target,"EOF amplitude target is not bracketed");
        for(unsigned k=0;k<30;++k) {
            const auto middle=(low+high)/2;
            if(run(middle,false,300,false,alternatives).score<target)low=middle;else high=middle;
        }
        const auto a=run((low+high)/2,false,300,false,alternatives),b=run((low+high)/2,true,300,false,alternatives);
        check(std::abs(a.score-target)<1e-4&&std::abs(a.score-b.score)<1e-8*(1+a.score),
            "partial EOF replay changed retained same-PCM statistics");
        check(a.bits==b.bits&&a.bits==(level?Bytes{0}:Bytes{}),
            "partial EOF prepayment lost a previously admissible pending bit");
        if(level<2)check(b.replays==1&&a.trials==b.trials&&std::abs(a.threshold-b.threshold)<1e-12,
            "cold partial EOF replay did not restore original finite-final threshold");
        else check(b.replays==0,"partial EOF rolled back already established output");
        if(level==1) {
            const auto canceled=run((low+high)/2,true,300,true,alternatives);
            check(canceled.bits==b.bits&&canceled.trials==b.trials&&canceled.replays==1,
                "canceled-then-retried finish changed partial EOF decisions");
            const auto early_a=run((low+high)/2,false,268,false,alternatives),early_b=run((low+high)/2,true,268,false,alternatives);
            check(early_a.bits==Bytes{0}&&early_b.bits==early_a.bits&&early_a.trials==early_b.trials&&early_b.replays==0,
                "EOF before first component execution changed observed partial-batch admission");
        }
    }    }
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
void parallel_long_continuation_exact_progress() {
    const Bytes expected{0,0,1};
    constexpr std::size_t delay=16;
    for(const bool keyed:{false,true}) {
        auto transmitted=config(64,keyed);
        transmitted.sample_rate=64;transmitted.bandwidth_hz=8;transmitted.carrier_hz=16;
        // A quarter-second remainder lets later private symbols separate
        // subsecond start phases that shared the first symbol's stream epoch.
        transmitted.integration_seconds=keyed?40.25:40;
        transmitted.stream_phase_samples=keyed?48:0;
        const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(transmitted));
        const auto samples=waveform(transmitted,expected,delay,3*symbol,.73,.003);
        auto received=transmitted;received.stream_phase_samples=0;
        modem::PatternSearch search;
        const auto step=.25*received.sample_rate/static_cast<double>(symbol);
        // Keep exact ties and both nominal-clock and coupled-clock fits in
        // their original order. One worker must produce byte-for-byte equal
        // decisions, diagnostics and evidence to parallel continuation.
        search.frequency_offsets_hz={0,0,-step,step,0,-step,step};
        search.couple_clock_to_carrier=true;search.uncoupled_frequency_count=4;
        search.search_stream_phases=keyed;
        // Public serial scoring and parallel scoring must agree even when
        // streamed rows can use an optional shared nominal waveform cache.
        search.prefer_streamed_templates=!keyed;
        modem::PatternReceiver path(received,2*1024*1024,search);
        check(!path.clock_windowed(),"long parallel fixture must exercise compact FFT tracking references");
        const auto payload_end=delay+expected.size()*symbol;
        const std::vector<float> partial(samples.begin(),samples.begin()+
            static_cast<std::ptrdiff_t>(payload_end+symbol/2));
        for(const unsigned workers:{3U,0U}) {
            try {
                const auto pending=compare_parallel_progress(partial,received,search,workers,false,false);
                check(pending.bits==expected && pending.pending_polls==expected.size() && pending.complete_events==0,
                      "parallel long continuation lost per-symbol pending bits or completed on a partial absent symbol");
                const auto complete=compare_parallel_progress(samples,received,search,workers,false,false);
                check(complete.bits==expected && complete.pending_polls==expected.size() && complete.complete_events==1,
                      "parallel long continuation failed exact pending bits and one physical completion without EOF");
                check(complete.completed_at>=payload_end+symbol,
                      "parallel long continuation ended before a complete absent symbol was observed");
            } catch(const Error& error) {
                throw Error(std::string(keyed?"private changing phase":"public mixed clock bank")+" / "+
                            std::to_string(workers)+" workers: "+error.what());
            }
        }
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
void continuous_long_fft_progress() {
    auto c=config(64);c.sample_rate=64;c.bandwidth_hz=8;c.carrier_hz=16;c.integration_seconds=40;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const Bytes expected{0,0,1};
    constexpr std::size_t workspace=2*1024*1024;
    constexpr std::array<std::size_t,1> regular{64};
    constexpr std::array<std::size_t,5> changing{7,137,1,31,83};
    for(const bool shared:{false,true})for(const auto delay:{std::size_t{16},3*symbol+16})
    for(const auto chunks:{std::span<const std::size_t>(regular),std::span<const std::size_t>(changing)}) {
        try {
            modem::PatternSearch search;
            search.worker_threads=1;search.candidate_limit=32;search.track_limit=4;search.bit_limit=128;
            // At forty seconds the compact FFT is shorter than twice the
            // symbol. Continuing each accepted symbol therefore also checks
            // the separately retained tracking reference.
            modem::PatternReceiver receiver(c,workspace,search);
            check(!receiver.clock_windowed(),"continuous long fixture must exercise the full FFT receiver");
            const auto samples=waveform(c,expected,delay,3*symbol,.73,.003);
            std::vector<std::complex<double>> projected(samples.size());
            for(std::size_t i=0;i<samples.size();++i)
                projected[i]=static_cast<double>(samples[i])*std::polar(1.,.37-2*std::numbers::pi*c.carrier_hz*i/c.sample_rate);
            Bytes observed;
            std::optional<std::uint64_t> first_sample;
            std::size_t position=0,chunk=0,pending=0,complete=0;
            const auto poll=[&] {
                check(receiver.working_bytes()<=workspace,"continuous long FFT receiver exceeded its workspace");
                for(const auto& event:receiver.take_bursts()) {
                    check(!event.missing_slots,"clean continuous long FFT receiver manufactured an unknown slot");
                    if(!first_sample)first_sample=event.stream_first_sample;
                    check(event.stream_first_sample==*first_sample,"continuous long FFT receiver changed stream identity");
                    observed.insert(observed.end(),event.bits.begin(),event.bits.end());
                    if(!event.bits.empty()) {
                        check(!event.complete,"continuous long FFT receiver withheld a payload bit until completion");
                        ++pending;
                    }
                    if(event.complete) {
                        check(position>=delay+(expected.size()+1)*symbol,
                              "continuous long FFT receiver completed before a fully observed absent symbol");
                        ++complete;
                    }
                }
                check(observed.size()<=expected.size() && std::equal(observed.begin(),observed.end(),expected.begin()),
                      "continuous long FFT receiver changed the exact pending bit prefix");
            };
            const auto advance=[&](std::size_t endpoint) {
                while(position<endpoint) {
                    const auto count=std::min(chunks[chunk++%chunks.size()],endpoint-position);
                    const auto raw=std::span(samples).subspan(position,count);
                    if(shared)receiver.push(raw,std::span(projected).subspan(position,count));
                    else receiver.push(raw);
                    position+=count;poll();
                }
            };
            for(std::size_t count=1;count<=expected.size();++count) {
                advance(delay+count*symbol+symbol/2);
                check(observed==Bytes(expected.begin(),expected.begin()+static_cast<std::ptrdiff_t>(count)) &&
                      pending==count && complete==0,
                      "continuous long FFT receiver hid an accepted bit behind a later symbol or FFT block");
            }
            advance(delay+(expected.size()+1)*symbol-1);
            check(complete==0,"six seconds of silence completed a still-partial long absent symbol");
            advance(samples.size());
            check(observed==expected && pending==expected.size() && complete==1,
                  "continuous long FFT receiver did not emit exact bits and one physical completion without EOF");
            check(receiver.take_bursts().empty(),"continuous long FFT progress polling duplicated events");

            modem::PatternReceiver background(c,workspace,search);
            std::mt19937_64 random(731);std::normal_distribution<float> noise;
            std::vector<float> quiet(samples.size());
            for(auto& sample:quiet)sample=noise(random);
            for(std::size_t i=0;i<quiet.size();++i)
                projected[i]=static_cast<double>(quiet[i])*std::polar(1.,.37-2*std::numbers::pi*c.carrier_hz*i/c.sample_rate);
            for(std::size_t offset=0,index=0;offset<quiet.size();++index) {
                const auto count=std::min(chunks[index%chunks.size()],quiet.size()-offset);
                const auto raw=std::span(quiet).subspan(offset,count);
                if(shared)background.push(raw,std::span(projected).subspan(offset,count));
                else background.push(raw);
                offset+=count;
                check(background.take_bursts().empty(),"continuous long FFT acquisition admitted noise-only input");
                check(background.working_bytes()<=workspace,"continuous long noise search exceeded its workspace");
            }
        } catch(const Error& error) {
            throw Error(std::string(shared?"shared projection":"raw PCM")+" / delay "+std::to_string(delay)+
                        " / "+std::to_string(chunks.size())+" chunk sizes: "+error.what());
        }
    }
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
// Set an evidence boundary with fixed noise draws, independently of the
// secret template bytes. This fixture uses the ordinary complex-bin path.
void set_tail_evidence(std::vector<float>& pcm,const modem::Config& c,
                       std::uint64_t symbol,unsigned bit,double target,std::uint64_t seed) {
    modem::PatternCode code(c,c.stream_epoch);
    const auto length=code.symbol_samples(),bin=modem::pattern_projection_bin_samples(c,0);
    const auto start=symbol*length;
    std::vector<float> clean(pcm.begin()+static_cast<std::ptrdiff_t>(start),
                             pcm.begin()+static_cast<std::ptrdiff_t>(start+length));
    std::vector<double> noise(length);std::mt19937_64 random(seed);
    std::normal_distribution<double> normal(0,1.);
    for(auto& value:noise)value=normal(random);
    const auto image=std::abs(modem::detail::pattern_projection_image_ratio(bin,c.carrier_hz,c.sample_rate));
    const auto condition=(1+image)/(1-image);
    const auto score=[&](double amplitude,unsigned candidate) {
        std::complex<double> dot{};double energy=0,norm=0;
        for(std::uint64_t first=0;first<length;first+=bin) {
            std::complex<double> observed{};
            for(std::uint64_t j=0;j<bin;++j) {
                const auto i=first+j;
                pcm[start+i]=static_cast<float>(amplitude*clean[i]+noise[i]);
                observed+=static_cast<double>(pcm[start+i])*std::polar(1.,-2*std::numbers::pi*
                    c.carrier_hz*static_cast<double>(start+i)/c.sample_rate);
            }
            const auto reference=code.value(symbol*code.chips_per_symbol()+first/code.chip_samples(),candidate);
            dot+=observed*std::conj(reference);norm+=std::norm(reference);energy+=std::norm(observed);
        }
        const auto fraction=std::clamp(std::norm(dot)/(condition*norm*energy),0.,1.-1e-15);
        return -(static_cast<double>(length/bin)-1)*std::log1p(-fraction);
    };
    double low=0,high=8;
    check(score(low,bit)<target && score(high,bit)>target,"tail fixture must bracket target evidence");
    for(unsigned iteration=0;iteration<32;++iteration) {
        const auto middle=(low+high)/2;
        if(score(middle,bit)<target)low=middle;else high=middle;
    }
    const auto amplitude=(low+high)/2,actual=score(amplitude,bit),other=score(amplitude,1-bit);
    check(std::abs(actual-target)<.01 && actual-other>1,"tail fixture must retain its intended bit preference");
}
void pending_tail_requires_joint_confidence() {
    auto c=config(64,true);c.pulse_shaping=false;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.initial_stream_symbols=1;
    search.false_alarm_probability=1e-8;
    constexpr std::array<std::size_t,3> changing{137,503,17};
    for(const bool joint_confident:{false,true}) {
        auto samples=waveform(c,{0,1,0},0,3*symbol,.73);
        set_tail_evidence(samples,c,1,1,8.,197);
        if(!joint_confident)set_tail_evidence(samples,c,2,0,34.,821);
        const std::array<std::size_t,1> whole{samples.size()};
        for(const auto chunks:{std::span<const std::size_t>(changing),std::span<const std::size_t>(whole)}) {
            const auto result=receive(samples,c,chunks,search);
            // The fixed middle/tail targets straddle standalone and joint
            // admission. A clean last symbol must still confirm both symbols.
            check(std::any_of(result.candidates.begin(),result.candidates.end(),[&](const auto& e) {
                return e.stream_symbol==1 && e.score>=search.retain_score && e.score<10 &&
                    e.score-e.alternative_score>=1;
            }),"weak-tail fixture must retain an individually unconfirmed middle-symbol candidate");
            if(joint_confident) {
                check(exact(result,{0,1,0}).first_stream_symbol==0,
                      "valid combined confidence must preserve the pending tail and entire admitted span");
                continue;
            }
            check(std::any_of(result.candidates.begin(),result.candidates.end(),[](const auto& e) {
                const auto combined=8.+e.score;
                const auto joint=combined-2-2*std::log(combined/2)-2*std::log(10.);
                return e.stream_symbol==2 && e.score>e.admission_threshold+1 && joint<e.admission_threshold-1;
            }),"tail fixture must independently pass while its joint weak tail remains unsupported");
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
void coupled_clock_progress_and_absence() {
    // Independent sampled clocks change both carrier and chip timing. These
    // +/-200-ppm cases cover the application clock range; the deliberately
    // wide +/-8000-ppm hypotheses stress both signs of timing correction.
    auto c=config(128);c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=4;c.pulse_shaping=false;
    const Bytes expected{0,0,1,0,1,1,0,1,0};
    constexpr std::size_t workspace=2*1024*1024;
    std::string failures;
    for(const auto ppm:{-200.,200.,-8000.,8000.}) {
        c.spreading_factor=std::abs(ppm)<=200?512:128;
        const auto symbol=modem::symbol_sample_count(c);
        modem::StreamingTransmitter transmitter(modem::RawBits{expected},c);
        modem::ChannelConfig impairment;impairment.clock_error_ppm=ppm;
        impairment.phase_noise_degrees_per_sqrt_second=0;impairment.snr_db=30;impairment.seed=731;
        modem::SampledSimulationChannel channel(c,impairment);
        std::array<float,173> block{};std::vector<float> samples;
        while(const auto count=channel.read(transmitter,block))
            samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        const auto capture_end=samples.size();
        samples.resize(capture_end+3*symbol);
        channel.read_noise(std::span(samples).subspan(capture_end));
        const auto partial_end=capture_end+symbol/2;
        for(const bool compact_hint:{false,true})for(const bool duplicate_nominal:{false,true}) {
            modem::PatternSearch search;search.couple_clock_to_carrier=true;
            const auto offset=c.carrier_hz*std::abs(ppm)*1e-6;
            search.frequency_offsets_hz={-offset,offset};
            if(duplicate_nominal) {
                search.uncoupled_frequency_count=2;
                search.frequency_offsets_hz.insert(search.frequency_offsets_hz.end(),{-offset,offset});
            }
            search.start_offset_seconds=0;search.start_uncertainty_seconds=.5;
            search.compact_clock_search=compact_hint;search.worker_threads=1;
            search.chunk_bits=64;search.bit_limit=64;search.track_limit=8;search.candidate_limit=128;
            if(ppm==-200 && !compact_hint && !duplicate_nominal) {
                rejects([&]{modem::PatternCorrelator receiver(c,search,workspace);},
                        "direct clock correlator accepted a coupled bank without global carrier competition");
                auto automatic=search;automatic.frequency_offsets_hz.clear();
                automatic.couple_clock_to_carrier=false;automatic.expand_clock_search=true;
                rejects([&]{modem::PatternCorrelator receiver(c,automatic,workspace);},
                        "direct clock correlator silently narrowed an expanded automatic carrier search");
            }
            const auto exercise=[&](std::size_t end,bool complete_expected) {
                modem::PatternReceiver receiver(c,workspace,search);
                check(!receiver.clock_windowed(),
                      "coupled-clock search must compare all carrier fits before publishing even with a compact hint");
                Bytes bits;std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
                std::size_t completions=0,bit_polls=0,position=0;
                std::uint64_t accepted_end=0;
                const auto poll=[&] {
                    check(receiver.working_bytes()<=workspace,"coupled carrier/clock bank exceeded its workspace");
                    const auto pending=receiver.provisional();
                    auto events=receiver.take_bursts();
                    bool has_bits=false;
                    for(const auto& event:events) {
                        const auto current=std::pair{event.stream_first_sample,event.stream_first_symbol};
                        if(!identity)identity=current;
                        check(current==*identity,"nominal and coupled carrier alternatives duplicated one physical stream");
                        check(event.missing_slots==0,"coupled-clock control lost a scored symbol");
                        has_bits|=!event.bits.empty();
                        bits.insert(bits.end(),event.bits.begin(),event.bits.end());
                        if(!event.bits.empty())accepted_end=event.end_sample;
                        if(bits.size()>expected.size() || !std::equal(bits.begin(),bits.end(),expected.begin())) {
                            std::string actual;for(const auto bit:bits)actual+=bit?'1':'0';
                            throw Error("coupled-clock progress changed or repeated an exact raw prefix: "+actual+
                                " at sample "+std::to_string(position)+" / carrier "+std::to_string(event.frequency_hz)+
                                " / first slot "+std::to_string(event.first_stream_symbol));
                        }
                        if(event.complete) {
                            ++completions;
                            // Start-grid uncertainty may place the admitted
                            // clock before the oracle source boundary. Every
                            // searched clock must still observe a whole failed
                            // slot after its own last accepted symbol.
                            check(position>=accepted_end+static_cast<std::uint64_t>(
                                      std::floor(symbol/(1+std::abs(ppm)*1e-6))),
                                  "coupled-clock reception completed before a whole absent symbol was observed");
                        }
                    }
                    if(has_bits) {
                        if(!bit_polls)check(bits.size()<8,"coupled-clock publication waited behind a complete byte");
                        ++bit_polls;
                    }
                    // A provisional accepted prefix must appear on this poll,
                    // regardless of the much larger storage chunk limit.
                    if(!pending.bits.empty())
                        check(bits.size()>=pending.first_stream_symbol+pending.bits.size(),
                              "coupled-clock acceptance was hidden behind a storage chunk");
                    check(!receiver.synchronized() || receiver.provisional().bits.empty() || completions,
                          "coupled-clock progress poll left accepted data undrained");
                    check(receiver.take_bursts().empty(),"coupled-clock drain repeated an accepted bit");
                };
                for(;position<end;) {
                    const auto count=std::min<std::size_t>(17,end-position);
                    receiver.push(std::span(samples).subspan(position,count));position+=count;poll();
                    if(position<partial_end)check(completions==0,"partial silence completed a coupled-clock stream");
                }
                receiver.finish();poll();receiver.finish();poll();
                check(bits==expected && bit_polls>=2,"coupled clocks must expose every exact bit across pending polls");
                check(completions==(complete_expected?1U:0U),
                      "coupled-clock EOF or fully observed absence produced the wrong completion state");
            };
            try {exercise(partial_end,false);exercise(samples.size(),true);}
            catch(const Error& error) {failures+=std::to_string(ppm)+" ppm / "+
                (compact_hint?"FFT with compact hint":"FFT")+" / "+
                (duplicate_nominal?"mixed bank":"coupled bank")+": "+error.what()+"\n";}
        }
    }
    if(!failures.empty())throw Error(failures);
}
void independent_fft_frequency_and_rate() {
    // RF conversion error is independent of PCM timing. Two equal-frequency
    // candidates with opposite rates must remain distinct through FFT scoring
    // and tracking; deriving rate from the final PCM carrier cannot recover
    // this capture. A shared-radio-reference policy uses the same pair API.
    auto c=config(128);c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=4;c.pulse_shaping=false;
    const Bytes expected{0,0,1,0,1,1,0,1,0};
    constexpr std::size_t workspace=2*1024*1024;
    for(const auto ppm:{-8000.,8000.}) {
        modem::StreamingTransmitter transmitter(modem::RawBits{expected},c);
        modem::ChannelConfig impairment;impairment.clock_error_ppm=ppm;
        impairment.frequency_offset_hz=.03125;impairment.phase_noise_degrees_per_sqrt_second=0;
        impairment.snr_db=30;impairment.seed=731;
        modem::SampledSimulationChannel channel(c,impairment);
        std::array<float,173> block{};std::vector<float> samples;
        while(const auto count=channel.read(transmitter,block))
            samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        const auto capture_end=samples.size(),symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
        samples.resize(capture_end+3*symbol);channel.read_noise(std::span(samples).subspan(capture_end));
        const auto offset=c.carrier_hz*ppm*1e-6+impairment.frequency_offset_hz;
        for(const bool legacy_rates:{false,true}) {
            modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=.5;
            search.worker_threads=1;search.drift_tolerant=false;search.chunk_bits=64;search.bit_limit=64;
            search.track_limit=8;search.candidate_limit=128;
            if(legacy_rates) {
                search.frequency_offsets_hz={offset};search.clock_errors_ppm={-8000,8000};
            } else {
                search.hypotheses={{offset,-8000},{offset,8000}};
                // Authoritative pairs must ignore unrelated legacy fields,
                // including values which are invalid as a standalone bank.
                search.frequency_offsets_hz={std::numeric_limits<double>::infinity()};
                search.clock_errors_ppm.clear();search.couple_clock_to_carrier=true;
                search.uncoupled_frequency_count=999;
            }
            modem::PatternReceiver receiver(c,workspace,search);
            check(!receiver.clock_windowed(),"frequency/rate FFT fixture unexpectedly selected a compact backend");
            Bytes observed;std::size_t completions=0,bit_polls=0,position=0;
            std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
            std::uint64_t accepted_end=0;
            const auto poll=[&] {
                check(receiver.working_bytes()<=workspace,"frequency/rate FFT bank exceeded its workspace");
                const auto pending=receiver.provisional();bool found=false;
                for(const auto& event:receiver.take_bursts()) {
                    const auto current=std::pair{event.stream_first_sample,event.stream_first_symbol};
                    if(!identity)identity=current;
                    check(current==*identity,"equal-frequency rate alternatives duplicated one physical stream");
                    check(event.missing_slots==0,"independent frequency/rate correction lost a symbol");
                    found|=!event.bits.empty();observed.insert(observed.end(),event.bits.begin(),event.bits.end());
                    if(!event.bits.empty())accepted_end=event.end_sample;
                    check(observed.size()<=expected.size() && std::equal(observed.begin(),observed.end(),expected.begin()),
                          "independent frequency/rate FFT correction changed the exact raw prefix");
                    if(event.complete) {
                        ++completions;
                        check(position>=accepted_end+static_cast<std::uint64_t>(std::floor(symbol/1.008)),
                              "frequency/rate FFT search completed before a whole absent symbol");
                    }
                }
                if(found) {
                    if(!bit_polls)check(observed.size()<8,"frequency/rate FFT publication waited for a byte");
                    ++bit_polls;
                }
                if(!pending.bits.empty())check(observed.size()>=pending.first_stream_symbol+pending.bits.size(),
                    "frequency/rate FFT search hid an accepted bit until another poll");
                check(receiver.take_bursts().empty(),"frequency/rate FFT drain repeated a decision");
            };
            while(position<samples.size()) {
                const auto count=std::min<std::size_t>(17,samples.size()-position);
                receiver.push(std::span(samples).subspan(position,count));position+=count;poll();
                if(position<capture_end+symbol/2)check(!completions,"partial silence completed a frequency/rate stream");
            }
            receiver.finish();poll();
            check(observed==expected && bit_polls>=2 && completions==1,
                  "independent frequency/rate FFT search did not preserve exact progress and observed absence");
        }
    }
}
void paired_carrier_alternatives_share_one_stream() {
    auto c=config(128);c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=4;c.pulse_shaping=false;
    const Bytes expected{0,0,1,0,1,1,0,1,0,1,0,0,1,1,0,1,1,1,0,0,1,0,1,0,0,0,1,1,1,0,0,1};
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto duration=modem::symbol_seconds(c);
    const auto samples=waveform(c,expected,0,3*symbol,.37,.001);
    modem::PatternSearch search;search.worker_threads=1;
    search.start_offset_seconds=0;search.start_uncertainty_seconds=.5;
    search.chunk_bits=64;search.bit_limit=128;
    for(const auto offset:{0.,-1.5/duration,1.5/duration,-1.25/duration,1.25/duration})
        search.hypotheses.push_back({offset,offset/10000000.*1e6});
    const std::array<std::size_t,4> chunks{17,113,509,41};
    const auto result=receive(samples,c,chunks,search,4*1024*1024);
    const auto& burst=exact(result,expected);
    check(burst.complete,"paired carrier alternatives did not complete after physical absence");
}
void frequency_rate_bank_limits() {
    auto c=config();modem::PatternSearch search;search.hypotheses={{0,0}};
    search.hypotheses.front().clock_error_ppm=std::numeric_limits<double>::infinity();
    rejects([&]{modem::PatternReceiver receiver(c,8*1024*1024,search);},"FFT accepted a nonfinite pair rate");
    search.hypotheses.front()={0,10001};
    rejects([&]{modem::PatternReceiver receiver(c,8*1024*1024,search);},"FFT accepted a pair rate outside its finite bound");
    search.hypotheses.front()={std::numeric_limits<double>::infinity(),0};
    rejects([&]{modem::PatternReceiver receiver(c,8*1024*1024,search);},"FFT accepted a nonfinite pair carrier");
    search.hypotheses.assign(modem::maximum_pattern_frequency_rate_hypotheses+1,{0,0});
    rejects([&]{modem::PatternReceiver receiver(c,8*1024*1024,search);},"FFT accepted an unbounded pair bank");
    search.hypotheses.clear();search.frequency_offsets_hz.assign(129,0);
    search.clock_errors_ppm.assign(modem::maximum_pattern_rate_hypotheses,0);
    rejects([&]{modem::PatternReceiver receiver(c,8*1024*1024,search);},"FFT expanded a legacy Cartesian bank past its finite pair limit");
    search.frequency_offsets_hz={c.carrier_hz*.01};search.clock_errors_ppm={0,0};
    search.couple_clock_to_carrier=true;
    modem::PatternReceiver boundary(c,8*1024*1024,search);
    check(boundary.working_bytes()<=8*1024*1024,"exact combined +10000-ppm boundary exceeded its workspace");
}
void attached_oscillator_policy_supplies_default_pairs() {
    auto c=config(128);c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=4;c.pulse_shaping=false;
    c.oscillator_search.emplace();auto& policy=*c.oscillator_search;
    policy.reference=modem::OscillatorReference::shared_radio;
    policy.rf_shift_hz=10000000;policy.rf.accuracy_ppm=.0001;
    const Bytes bits{0,0,1,0};
    const auto samples=waveform(c,bits,0,3*modem::symbol_sample_count(c),.31,.001);
    modem::PatternSearch implicit;implicit.worker_threads=1;implicit.start_offset_seconds=0;
    implicit.start_uncertainty_seconds=.5;implicit.retain_score=0;
    auto explicit_pairs=implicit;explicit_pairs.hypotheses=modem::oscillator_pattern_search(c).hypotheses;
    const std::array<std::size_t,2> chunks{137,61};
    const auto a=receive(samples,c,chunks,implicit),b=receive(samples,c,chunks,explicit_pairs);
    check(a.bursts.size()==b.bursts.size() && a.candidates.size()==b.candidates.size(),
          "attached oscillator policy did not supply the same default search as explicit pairs");
    for(std::size_t i=0;i<a.bursts.size();++i)
        check(same_burst(a.bursts[i],b.bursts[i]),"implicit oscillator policy changed stream publication");
    for(std::size_t i=0;i<a.candidates.size();++i)
        check(same_evidence(a.candidates[i],b.candidates[i]),"implicit oscillator policy changed paired evidence");
    check(exact(a,bits).complete,"implicit shared-radio policy did not decode and observe absence");
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
    const auto cached_bytes=pressure.working_bytes();
    // Keep this cache-retention fixture just above its fixed control reservation.
    // Bounded timing-component metadata adds about 4 KiB to each receiver; the
    // payload/eviction assertions below remain at the same full bit limit.
    constexpr std::size_t pressure_budget=328*1024;
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
void streamed_template_rows_preserve_exact_search() {
    auto c=config(128,true);
    // This is an exact algorithm/cache comparison across serial, parallel and
    // workspace dispatch. Automatic arithmetic may deliberately choose FP32
    // for the serial streamed member and FP64 for the other members. Keep the
    // original exact-score assertions on the independent FP64 reference;
    // native_fp32_private_template_progress separately compares Default,
    // FP32 minimum and that reference on identical PCM at every progress poll.
    c.search_arithmetic=modem::SearchArithmetic::fp64;
    modem::PatternSearch search;search.initial_stream_symbols=1;
    search.candidate_limit=31;search.track_limit=4;search.bit_limit=128;
    const auto duration=modem::symbol_seconds(c);
    for(int i=-64;i<=64;++i)search.frequency_offsets_hz.push_back(i/(256.*duration));
    constexpr std::array<std::size_t,3> limits{8*1024*1024,512*1024,512*1024};
    constexpr std::array<std::size_t,3> workers{1,1,3};
    std::array<std::unique_ptr<modem::PatternReceiver>,3> receivers;
    for(std::size_t i=0;i<receivers.size();++i) {
        search.worker_threads=workers[i];
        receivers[i]=std::make_unique<modem::PatternReceiver>(c,limits[i],search);
        check(!receivers[i]->clock_windowed(),"streamed templates must retain FFT search without a clock hint");
        check(receivers[i]->working_bytes()<=limits[i],"template construction exceeded its memory limit");
    }
    check(receivers[0]->working_bytes()>limits[1],"fixture must require streaming at its reduced memory limit");
    const Bytes expected{0,1,1};
    const auto samples=waveform(c,expected,73,static_cast<std::size_t>(modem::pattern_absence_samples(c)+2*modem::symbol_sample_count(c)),.37,.03);
    Bytes observed;std::size_t complete=0;
    const auto poll=[&] {
        const auto reference_bursts=receivers[0]->take_bursts();
        const auto reference_candidates=receivers[0]->candidates();
        for(std::size_t i=1;i<receivers.size();++i) {
            check(receivers[i]->working_bytes()<=limits[i],"streamed FFT search exceeded its current memory limit");
            const auto bursts=receivers[i]->take_bursts();
            check(bursts.size()==reference_bursts.size(),"streamed templates changed the next progress poll");
            for(std::size_t j=0;j<bursts.size();++j)
                check(same_burst(bursts[j],reference_bursts[j]),"streamed templates changed bits, timing or physical completion");
            const auto candidates=receivers[i]->candidates();
            check(candidates.size()==reference_candidates.size(),"streamed templates changed hypothesis retention");
            for(std::size_t j=0;j<candidates.size();++j)
                check(same_evidence(candidates[j],reference_candidates[j]),"streamed templates changed scores or trial penalties");
        }
        for(const auto& burst:reference_bursts) {
            observed.insert(observed.end(),burst.bits.begin(),burst.bits.end());complete+=burst.complete;
        }
    };
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min<std::size_t>(257,samples.size()-offset);
        for(auto& receiver:receivers)receiver->push(std::span(samples).subspan(offset,count));
        offset+=count;poll();
    }
    for(auto& receiver:receivers)receiver->finish();poll();
    check(observed==expected && complete==1,"streamed FFT fixture must recover exact bits and one observed physical end");

    auto narrow=c;narrow.bandwidth_hz=1;
    modem::PatternSearch expanded;expanded.expand_clock_search=true;expanded.compact_clock_search=true;
    expanded.start_offset_seconds=0;expanded.start_uncertainty_seconds=7;expanded.worker_threads=1;
    constexpr std::size_t private_limit=2*1024*1024;
    modem::PatternReceiver private_bank(narrow,private_limit,expanded);
    check(!private_bank.clock_windowed(),"private expanded clock bank must compare complete FFT hypotheses despite compact hint");
    check(private_bank.working_bytes()<=private_limit,"private expanded FFT metadata exceeded its bounded workspace");
    std::array<float,64> short_noise{};private_bank.push(short_noise);private_bank.finish();
    check(private_bank.take_bursts().empty(),"short private capture and EOF must not fabricate a fully scored symbol");
    rejects([&]{modem::PatternReceiver unaffordable(narrow,64*1024,expanded);},
            "unaffordable expanded FFT search silently fell back to premature clock-lane admission");
}
void application_local_fallback_preserves_original_search() {
    auto c=config(16384,true);c.sample_rate=4000;c.carrier_hz=1000;c.bandwidth_hz=1000;c.pulse_shaping=false;
    modem::PatternSearch automatic;automatic.expand_clock_search=true;
    automatic.start_offset_seconds=.002;automatic.start_uncertainty_seconds=0;
    automatic.worker_threads=1;automatic.candidate_limit=31;automatic.track_limit=4;automatic.bit_limit=128;
    constexpr std::size_t limit=1024*1024;
    rejects([&]{modem::PatternReceiver strict(c,limit,automatic);},
            "low-level expanded search must still reject insufficient FFT core memory");
    automatic.allow_local_clock_fallback=true;
    modem::PatternReceiver fallback(c,limit,automatic);
    check(fallback.clock_windowed() && fallback.local_clock_fallback(),
          "application fallback must identify its reduced nominal-clock coverage");
    check(fallback.working_bytes()<=limit,"application local fallback exceeded its memory ceiling");
    auto explicit_coupled=automatic;explicit_coupled.frequency_offsets_hz={0,.15};explicit_coupled.couple_clock_to_carrier=true;
    rejects([&]{modem::PatternReceiver strict(c,limit,explicit_coupled);},
            "application opt-in must not shrink explicit coupled frequency banks");
    auto no_window=automatic;no_window.start_offset_seconds.reset();
    rejects([&]{modem::PatternReceiver strict(c,limit,no_window);},
            "application fallback must not invent a system-clock search window");
    auto nondefault_clock=automatic;nondefault_clock.clock_errors_ppm={100};
    modem::PatternReceiver complete_clock_bank(c,limit,nondefault_clock);
    check(complete_clock_bank.clock_windowed() && !complete_clock_bank.local_clock_fallback() &&
          complete_clock_bank.working_bytes()<=limit,
          "an affordable compact fallback must retain the complete explicit clock-rate bank");

    auto local=automatic;local.expand_clock_search=false;local.allow_local_clock_fallback=false;
    local.compact_clock_search=automatic.compact_clock_search;
    const auto step=.25/modem::symbol_seconds(c);
    local.frequency_offsets_hz={0,-step,step,-2*step,2*step};
    modem::PatternReceiver original(c,limit,local);
    check(!original.local_clock_fallback(),"explicit local search must not be presented as an automatic fallback");
    const Bytes expected{0,1,1};
    const auto samples=waveform(c,expected,8,static_cast<std::size_t>(modem::pattern_absence_samples(c)+16),.37,.01);
    Bytes observed;std::size_t complete=0;
    const auto poll=[&] {
        const auto reference=original.take_bursts(),reduced=fallback.take_bursts();
        check(reference.size()==reduced.size(),"application fallback changed original local progress timing");
        for(std::size_t i=0;i<reference.size();++i) {
            check(same_burst(reference[i],reduced[i]),"application fallback changed the original five-bin decisions");
            observed.insert(observed.end(),reduced[i].bits.begin(),reduced[i].bits.end());complete+=reduced[i].complete;
        }
        const auto a=original.candidates(),b=fallback.candidates();
        check(a.size()==b.size(),"application fallback lost original local hypotheses");
        for(std::size_t i=0;i<a.size();++i)
            check(same_evidence(a[i],b[i]),"application fallback changed local evidence or trial penalties");
        check(fallback.working_bytes()<=limit,"application fallback grew beyond its memory ceiling");
    };
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min<std::size_t>(2048,samples.size()-offset);
        const auto block=std::span(samples).subspan(offset,count);
        original.push(block);fallback.push(block);offset+=count;poll();
    }
    original.finish();fallback.finish();poll();
    check(observed==expected && complete==1,"application local fallback lost exact bits or observed physical completion");

    auto narrow=config(128,true);narrow.bandwidth_hz=1;
    auto cached=automatic;cached.compact_clock_search=false;cached.allow_local_clock_fallback=false;
    // Retain coverage of the original optional full-row cache. Drift scoring
    // always streams section templates, even with spare workspace.
    cached.drift_tolerant=false;
    modem::PatternReceiver cached_bank(narrow,64*1024*1024,cached);
    cached.prefer_streamed_templates=true;
    modem::PatternReceiver shared_bank(narrow,64*1024*1024,cached);
    check(!shared_bank.clock_windowed() && !shared_bank.local_clock_fallback(),
          "sharing template memory must retain complete expanded FFT coverage");
    check(shared_bank.working_bytes()<2*1024*1024 && cached_bank.working_bytes()>8*1024*1024,
          "multi-bank streaming preference did not leave cached-row RAM for peer receivers");
    cached.drift_tolerant=true;cached.prefer_streamed_templates=false;
    modem::PatternReceiver section_bank(narrow,64*1024*1024,cached);
    check(!section_bank.clock_windowed()&&!section_bank.local_clock_fallback()&&
          section_bank.working_bytes()<2*1024*1024,
          "drift section scoring must retain full clock coverage without allocating full template rows");
}
}
namespace {
void candidate_quota_health() {
    auto c=config(128,true);c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1200;c.pulse_shaping=false;
    auto shifted=c;shifted.carrier_hz+=75;
    auto samples=waveform(c,Bytes{0},0,0,.21);
    const auto other=waveform(shifted,Bytes{1},0,0,-.53);
    for(std::size_t i=0;i<samples.size();++i)samples[i]+=other[i];
    for(const unsigned limit:{0U,1U,2U}) {
        modem::PatternSearch search;search.frequency_offsets_hz={0,75};search.drift_tolerant=false;
        search.worker_threads=1;search.chunk_bits=16;search.candidate_limit=limit==1?1:16;search.track_limit=limit==2?1:4;
        modem::PatternReceiver receiver(c,8*1024*1024,search);
        check(!receiver.clock_windowed() && !receiver.candidate_limited(),"quota fixture did not start in healthy FFT backend");
        receiver.push(samples);receiver.finish();
        const auto bursts=receiver.take_bursts();
        check(receiver.candidate_limited()==(limit!=0),"FFT candidate quota did not report omitted coverage");
        check(bursts.size()==(limit?1:2),"quota fixture did not admit its independent control streams");
        for(const auto& burst:bursts)check(!burst.complete && burst.bits.size()==1,"quota/EOF manufactured physical completion");
        receiver.finish();check(receiver.candidate_limited()==(limit!=0),"draining or EOF cleared quota history");
    }
    // Noise and ordinary competition between overlapping starts are healthy.
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.candidate_limit=1;search.track_limit=1;
    search.drift_tolerant=false;search.worker_threads=1;
    for(const unsigned background:{0U,1U,2U}) {
        modem::PatternReceiver receiver(c,8*1024*1024,search);
        auto input=waveform(c,Bytes{0},0,0,.21);
        if(background)std::fill(input.begin(),input.end(),0);
        if(background==2) {
            std::mt19937_64 random(481);std::normal_distribution<float> noise(0,.2f);
            for(auto& sample:input)sample=noise(random);
        }
        receiver.push(input);receiver.finish();receiver.take_bursts();
        check(!receiver.candidate_limited(),"noise or ordinary candidate rejection reported quota loss");
    }
}
void initial_search_coverage() {
    auto c=config();c.sample_rate=8192;c.carrier_hz=2048;c.bandwidth_hz=1024;c.pulse_shaping=false;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto seconds=static_cast<double>(symbol)/c.sample_rate;
    const std::vector<float> silence(10*symbol);
    modem::PatternSearch search;search.start_offset_seconds=0;search.frequency_offsets_hz={0};
    search.drift_tolerant=false;search.worker_threads=1;
    modem::PatternReceiver receiver(c,8*1024*1024,search);
    check(!receiver.clock_windowed(),"acquisition coverage fixture unexpectedly selected the correlator");
    check(!receiver.initial_search_complete(),"unobserved FFT acquisition claimed coverage");
    receiver.push(std::span(silence).first(symbol));
    check(!receiver.initial_search_complete(),"consumed FFT symbol bypassed acquisition batch lookahead");
    receiver.push(std::span(silence).first(3*symbol));
    check(receiver.initial_search_complete(),"completed FFT acquisition did not report coverage");
    check(!receiver.acquiring() && receiver.take_bursts().empty(),"noise coverage manufactured reception");
    receiver.finish();
    check(receiver.initial_search_complete(),"EOF discarded completed acquisition coverage");

    search.start_offset_seconds=4*seconds;search.start_uncertainty_seconds=seconds;
    modem::PatternReceiver future(c,8*1024*1024,search);
    future.push(std::span(silence).first(3*symbol));
    check(!future.initial_search_complete(),"FFT coverage ignored the original future search horizon");
    future.push(std::span(silence).first(7*symbol));
    check(future.initial_search_complete(),"FFT coverage did not reach the complete future search horizon");

    search.start_offset_seconds=-seconds;search.start_uncertainty_seconds=0;
    modem::PatternReceiver past(c,8*1024*1024,search);
    check(!past.initial_search_complete(),"negative start window skipped all FFT acquisition");
    past.push(std::span(silence).first(4*symbol));
    check(past.initial_search_complete(),"negative start window never completed its first FFT acquisition");

    search.start_offset_seconds=0;
    modem::PatternReceiver partial(c,8*1024*1024,search);
    partial.push(std::span(silence).first(symbol/2));partial.finish();
    check(!partial.initial_search_complete(),"EOF supplied missing FFT acquisition samples");
    search.frequency_offsets_hz={-16,0,16};search.couple_clock_to_carrier=true;
    modem::PatternReceiver stretched(c,8*1024*1024,search);
    stretched.push(std::span(silence).first(symbol));stretched.finish();
    check(!stretched.initial_search_complete(),"nominal FFT duration omitted the stretched clock hypothesis");

    search.frequency_offsets_hz={0};search.couple_clock_to_carrier=false;search.start_offset_seconds.reset();
    modem::PatternReceiver unbounded(c,8*1024*1024,search);
    unbounded.push(std::span(silence).first(4*symbol));
    check(!unbounded.initial_search_complete(),"unbounded FFT search invented a finite initial horizon");
}
}
int main(int argc,char** argv) {
    unsigned failures=0;
    const auto run=[&](const char* name,auto test) {
        if(argc>1 && std::string(name).find(argv[1])==std::string::npos)return;
        try { test();std::cout<<name<<": passed\n"; }
        catch(const std::exception& error){++failures;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("candidate quota health",candidate_quota_health);
    run("initial search coverage",initial_search_coverage);
    run("exact blind bits",exact_blind_bits);run("chunk invariance and late start",changing_chunks_and_late_start);
    run("continuous long FFT progress",continuous_long_fft_progress);
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
    run("coupled clock progress and absence",coupled_clock_progress_and_absence);
    run("independent FFT frequency and rate",independent_fft_frequency_and_rate);
    run("paired carrier alternatives share one stream",paired_carrier_alternatives_share_one_stream);
    run("frequency rate bank limits",frequency_rate_bank_limits);
    run("attached oscillator policy supplies default pairs",attached_oscillator_policy_supplies_default_pairs);
    run("high-SNR sampled private and public patterns",high_snr_sampled_channel);
    run("orthogonal private pattern bins",orthogonal_private_pattern_bins);
    run("shared projection and workspace updates",shared_projection_and_workspace_update);
    run("short pattern shared projection phase",short_pattern_shared_projection_phase);
    run("short template cache workspace and exact equivalence",short_template_cache_workspace);
    run("parallel search exact progress",parallel_search_exact_progress);
    run("outer DSSS progress and physical absence",outer_dsss_progress_and_absence);
    run("outer DSSS weak AWGN unchanged",outer_dsss_weak_awgn_preserves_admission);
    run("long timing guide immutable progress",long_timing_guide_preserves_bit_owner);
    run("qualified UTC clock window",qualified_clock_window);
    run("qualified UTC FFT geometry",qualified_fft_window_geometry);
    run("qualified UTC FFT progress",qualified_fft_window_preserves_progress);
    run("qualified UTC FFT restricted direct",qualified_fft_restricted_direct);
    run("qualified UTC FFT partitioned reception",qualified_fft_partitioned_reception);
    run("search arithmetic retained progress",search_arithmetic_preserves_retained_progress);
    run("native FP32 whole progress",native_fp32_whole_preserves_progress);
    run("automatic workers select native private search",automatic_workers_select_native_private_search);
    run("native FP32 private template progress",native_fp32_private_template_progress);
    run("qualified UTC FFT bounded readiness",qualified_fft_bounded_readiness);
    run("qualified UTC FFT follow-on progress",qualified_fft_followon_progress);
    run("qualified UTC FFT component progress",qualified_fft_component_progress);
    run("qualified UTC FFT partial EOF threshold",qualified_fft_partial_eof_threshold);
    run("parallel search physical absence",parallel_search_physical_absence);
    run("parallel long continuation exact progress",parallel_long_continuation_exact_progress);
    run("bounded long clock-window fallback",long_clock_window_fallback);
    run("streamed template rows preserve exact search",streamed_template_rows_preserve_exact_search);
    run("application local fallback preserves original search",application_local_fallback_preserves_original_search);
    run("hardware settling remains outside payload",hardware_settling_is_not_payload);
    return failures?1:0;
}
