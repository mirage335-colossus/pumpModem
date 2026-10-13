#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <bit>
#include <random>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
modem::Config configuration(unsigned multiplier=10,unsigned factor=1) {
    modem::Config c;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=50./factor;
    c.spreading_factor=factor==1?256:64;c.scramble=true;c.dsss=factor>1;c.dsss_factor=factor;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    Bytes key(32);for(std::size_t i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(i*7+3);
    c.data_key.emplace(key);c.spreading_seed[3]=51;c.dsss_seed[13]=171;
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=multiplier;plan->group_hops=2;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
    plan->ramp_seconds=.01;c.scheduled_hopping=plan;
    modem::SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    return c;
}
modem::PatternSearch search(const modem::Config& c) {
    modem::PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=0;
    s.frequency_offsets_hz={0};s.clock_errors_ppm={0};s.drift_tolerant=false;s.worker_threads=1;
    s.input_sample_origin=0;return s;
}
struct Fixture {
    modem::Config config;
    modem::SchedHopSourceMap map;
    modem::PatternCode code;
    modem::SchedHopCoordinate origin,source;
    std::unique_ptr<modem::ScheduledSymbolCalendar> calendar;
    static double noise_at(std::uint64_t sample) {
        const auto mix=[](std::uint64_t x) {x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
            x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);};
        const auto a=(static_cast<double>(mix(sample^529)>>11)+.5)/9007199254740992.;
        const auto b=(static_cast<double>(mix(sample^0xda942042e4dd58b5ULL)>>11)+.5)/9007199254740992.;
        return .002*std::sqrt(-2*std::log(a))*std::cos(2*std::numbers::pi*b);
    }
    explicit Fixture(modem::Config c):config(c),map(*c.scheduled_hopping,*c.data_key,c.sample_rate),
        code(c,c.stream_epoch),origin{c.stream_epoch*c.sample_rate+c.stream_phase_samples,0},source(map.locate(origin).source) {
        if(modem::packed_scheduled_symbols(c))calendar=std::make_unique<modem::ScheduledSymbolCalendar>(c);
    }
    std::uint64_t source_start(std::uint64_t index) const {
        return calendar?calendar->offset_source(config.stream_epoch,config.stream_phase_samples,index):index*code.symbol_samples();
    }
    float sample(std::uint64_t at,const Bytes& bits) {
        const auto physical=origin.add_samples(at);const auto cell=map.locate(physical);
        const auto position=cell.source.distance(source);const auto symbol=code.symbol_samples();
        std::complex<double> shaped{};
        auto index=position<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(position/symbol);
        if(calendar && position>=0) {
            index=calendar->lower_bound_source(config.stream_epoch,config.stream_phase_samples,
                static_cast<std::uint64_t>(std::ceil(position)));
            if(index && static_cast<long double>(source_start(index-1))+symbol>position)--index;
        }
        if(cell.active && index<bits.size() && position>=source_start(index) && position<source_start(index)+symbol) {
            const auto within=static_cast<double>(position-source_start(index));
            shaped=modem::pattern_limit_pcm(std::sqrt(2*modem::nominal_signal_power)*
                modem::outer_dsss_transmit_gain(config)*code.shaped_value(index*code.chips_per_symbol(),bits[index],within));
            shaped*=modem::SchedHopSourceMap::gain(cell,physical);
        }
        const auto carrier=std::polar(1.,2*std::numbers::pi*config.carrier_hz*static_cast<double>(at)/config.sample_rate);
        return static_cast<float>((carrier*shaped).real()+noise_at(at));
    }
    std::uint64_t boundary(std::uint64_t symbols,bool end=true)const {
        const auto offset=symbols?source_start(symbols-1)+code.symbol_samples():0;
        return map.source_to_physical(source.add_samples(offset),end).sample-origin.sample;
    }
};
struct Result {Bytes bits;bool complete=false;std::uint64_t first_publish=0,completion=0;modem::PatternCorrelatorWork work;};
Result receive(const modem::Config& c,const Bytes& bits,bool skipping,std::size_t block) {
    Fixture fixture(c);const auto s=search(c);
    modem::PatternCorrelator receiver(c,s,16*1024*1024,{.amplitude_only=true});
    const auto failed=std::max<std::uint64_t>(1,(6ULL*c.sample_rate+fixture.code.symbol_samples()-1)/fixture.code.symbol_samples());
    const auto count=fixture.boundary(bits.size()+failed)+2*modem::pattern_amplitude_geometry(c,s).fft_samples;
    std::vector<float> pcm(block);Result result;
    for(std::uint64_t at=0;at<count;) {
        // A null certificate may split the input operation, but must not move
        // the paired consumer's progress-poll lattice. Compare publication on
        // identical original block endpoints, not two different callback grids.
        const auto poll_end=at+std::min<std::uint64_t>(block,count-at);
        while(at<poll_end) {
            auto n=static_cast<std::size_t>(poll_end-at);
            bool skipped=false;
            if(skipping)if(const auto end=receiver.unobserved_end_from(at);end && *end>at) {
                n=static_cast<std::size_t>(std::min<std::uint64_t>(n,*end-at));
                skipped=receiver.try_skip_unobserved(n);
                check(skipped,"certified null interval must pass the matching skip verifier");
            }
            if(!skipped) {
                for(std::size_t i=0;i<n;++i)pcm[i]=fixture.sample(at+i,bits);
                receiver.push(std::span(pcm).first(n));
            } // Noise uses absolute sample addresses; skips need no RNG advancement.
            at+=n;
        }
        for(const auto& burst:receiver.take_bursts()) {
            check(!burst.missing_slots,"scheduled nulls must not create missing payload slots");
            if(!burst.bits.empty()) {
                if(!result.first_publish)result.first_publish=at;
                result.bits.insert(result.bits.end(),burst.bits.begin(),burst.bits.end());
            }
            if(burst.complete){result.complete=true;result.completion=at;}
        }
        check(receiver.working_bytes()<=16*1024*1024,"scheduled receiver exceeded workspace");
    }
    receiver.finish();for(const auto& burst:receiver.take_bursts())check(!burst.complete,"EOF must not manufacture scheduled absence");
    result.work=receiver.work();return result;
}
void geometry_and_sparse_semantics() {
    auto c=configuration();const auto g=modem::pattern_amplitude_geometry(c,search(c));
    check(g.supported,"bounded active-only amplitude geometry must be supported");
    check(g.fft_samples<c.sample_rate*.8,"FFT must fit a complete active portion");
    auto broad=search(c);broad.search_stream_phases=true;broad.start_uncertainty_seconds=6;
    bool rejected=false;try{modem::PatternCorrelator tiny(c,broad,128*1024,{.amplitude_only=true});}
    catch(const Error&){rejected=true;}
    check(rejected,"complete broad canonical timing coverage must reject insufficient workspace");
    Fixture fixture(c);const auto first=fixture.boundary(1);
    const auto raw=receive(c,{1},false,17),sparse=receive(c,{1},true,17);
    check(raw.bits==Bytes{1} && sparse.bits==raw.bits,"scheduled whole-bit amplitude reception must preserve exact bits");
    check(raw.complete && sparse.complete,"complete eligible absence must finish scheduled reception");
    if(!(sparse.first_publish<=raw.first_publish && raw.first_publish-sparse.first_publish<=g.fft_samples+17 &&
        sparse.completion<=raw.completion && raw.completion-sparse.completion<=g.fft_samples+17))
        std::cerr<<"paired endpoints raw="<<raw.first_publish<<','<<raw.completion<<" sparse="
            <<sparse.first_publish<<','<<sparse.completion<<" FFT="<<g.fft_samples
            <<" observations="<<raw.work.amplitude_observations<<','<<sparse.work.amplitude_observations
            <<" transforms="<<raw.work.amplitude_transforms<<','<<sparse.work.amplitude_transforms<<'\n';
    check(sparse.first_publish<=raw.first_publish && raw.first_publish-sparse.first_publish<=g.fft_samples+17 &&
        sparse.completion<=raw.completion && raw.completion-sparse.completion<=g.fft_samples+17,
        "proven-null advance must preserve physical endpoints and may settle an already observed partial window sooner");
    check(raw.first_publish>=first && raw.first_publish-first<=2*g.fft_samples+17,
        "bit must publish near its fully observed physical source endpoint");
    check(sparse.work.scheduled_null_samples>0,"scheduled nulls must avoid actual input/scoring work");
    check(raw.work.amplitude_transforms==sparse.work.amplitude_transforms &&
        raw.work.amplitude_observations==sparse.work.amplitude_observations,
        "null optimization must retain identical observed FFTs and candidate updates");
}
void arithmetic_and_outer_geometry() {
    auto c=configuration();c.search_arithmetic=modem::SearchArithmetic::fp64;
    const auto wide=receive(c,{0},true,113);
    c.search_arithmetic=modem::SearchArithmetic::fp32;const auto narrow=receive(c,{0},true,113);
    check(wide.bits==Bytes{0} && narrow.bits==wide.bits,"FP32 and FP64 scheduled arithmetic must retain the bit");
    check(wide.work.amplitude_float_transforms==0 && narrow.work.amplitude_float_transforms>0,
        "scheduled arithmetic setting must select native executed FFT precision");
    for(auto factor:{10U,100U,1000U}) {
        c=configuration(10,factor);const auto g=modem::pattern_amplitude_geometry(c,search(c));
        check(g.supported,"interleaved DSSS active-only geometry should retain full spectral margins");
        check(g.fft_samples<=2*modem::pattern_chip_samples(c),"DSSS envelope observations must not erase fine chips");
    }
}
void canonical_cell_end_and_following_bit() {
    auto c=configuration();c.integration_seconds=24;
    const auto cell=modem::canonical_phase_step(c);
    check(cell>1,"canonical boundary fixture must retain a nontrivial exact address cell");
    auto tick=c.stream_epoch*c.sample_rate+c.stream_phase_samples+cell-1;
    c.stream_epoch=tick/c.sample_rate;c.stream_phase_samples=tick%c.sample_rate;
    Fixture fixture(c);auto exact=search(c),grouped=exact;grouped.search_stream_phases=true;
    grouped.qualified_start_window=modem::PatternStartWindow{-static_cast<long double>(c.stream_phase_samples),1,0};
    modem::PatternCorrelator receiver(c,grouped,32*1024*1024,{.amplitude_only=true});
    const Bytes expected{1,0};const auto first_end=fixture.boundary(1),second_end=fixture.boundary(2),absence_end=fixture.boundary(3);
    std::array<float,113> pcm{};Bytes result;bool complete=false;
    for(std::uint64_t at=0;at<absence_end+256;) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),absence_end+256-at));
        for(std::size_t i=0;i<n;++i)pcm[i]=fixture.sample(at+i,expected);
        receiver.push(std::span(pcm).first(n));at+=n;
        for(const auto& burst:receiver.take_bursts()) {
            if(!burst.bits.empty()) {
                check(at>=first_end,"canonical cell END must not publish before the actual final source sample");
                result.insert(result.end(),burst.bits.begin(),burst.bits.end());
                if(result.size()>1)check(at>=second_end,"a delayed cell END must not assign the next bit prematurely");
            }
            if(burst.complete){check(at>=absence_end,"null gaps/cell slack must not substitute for a fully scored absent symbol");complete=true;}
        }
    }
    check(result==expected && complete,"canonical cell group boundary must preserve consecutive bits and completion");
}
void long_chip_flat_safe_geometry() {
    auto c=configuration(10000);c.bandwidth_hz=4;c.integration_seconds=3600;
    auto s=search(c);s.start_uncertainty_seconds=6;s.search_stream_phases=true;
    s.admitted_clock_half_width_ppm=.00060000000009;
    const auto geometry=modem::pattern_amplitude_geometry(c,s);
    check(geometry.supported,"hour-long source symbols must retain flat-safe scheduled FFT geometry");
    check(geometry.receiver_phase_step_samples<modem::canonical_phase_step(c) &&
        modem::canonical_phase_step(c)%geometry.receiver_phase_step_samples==0,
        "flat-safe phase subdivision must preserve every original cipher-address representative");
    check(geometry.receiver_timing_subdivisions>1,
        "long chips must refine the original half-chip timing intervals");
    check(geometry.clock_hypotheses<=65,"complete GPSDO clock coverage must fit the bounded refined grid");
    const auto fine=modem::pattern_chip_samples(c);
    const auto map=modem::SchedHopSourceMap(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto width=map.minimum_flat_samples();
    check(geometry.receiver_phase_step_samples<=width/8 &&
        fine/(2.L*geometry.receiver_timing_subdivisions)<=width/4.L,
        "phase and arrival coverage cells must fit the active flat independently of chip duration");
    const auto phase=geometry.receiver_phase_step_samples;
    check(modem::pattern_amplitude_phase_cell_upper(c,geometry,phase)-phase<geometry.receiver_phase_step_samples,
        "refined phase cells must remain bounded and contiguous");
}
void packed_short_slots_and_absence() {
    auto c=configuration();c.sample_rate=16384;c.carrier_hz=4096;c.bandwidth_hz=2048;c.spreading_factor=256;
    // A quarter-second symbol with only 10ms ramps is safely split. Use a
    // genuine guard-dominated case to exercise full-gain slot deferral instead.
    auto guarded=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);
    guarded->clock_error_seconds=guarded->peer_clock_error_seconds=.05;
    guarded->audio_error_seconds=.1;c.scheduled_hopping=std::move(guarded);
    check(modem::packed_scheduled_symbols(c),"short symbols must use the joint packed calendar");
    modem::ScheduledSymbolCalendar calendar(c);
    const auto start=calendar.physical_slot(calendar.first_rank(c.stream_epoch,c.stream_phase_samples));
    c.stream_epoch=start/c.sample_rate;c.stream_phase_samples=start%c.sample_rate;
    const auto geometry=modem::pattern_amplitude_geometry(c,search(c));
    check(geometry.supported,"full-gain short calendar must retain a useful amplitude geometry");
    const Bytes bits{1,0};const auto raw=receive(c,bits,false,211),sparse=receive(c,bits,true,211);
    check(raw.bits==bits && sparse.bits==bits && raw.complete && sparse.complete,
        "packed slots must preserve bit order and six seconds of scored eligible absence");
    Fixture fixture(c);
    const auto failures=(6ULL*c.sample_rate+fixture.code.symbol_samples()-1)/fixture.code.symbol_samples();
    const auto absence=fixture.boundary(bits.size()+failures);
    check(raw.completion>=absence && sparse.completion>=absence,
        "packed null/wait intervals must not cause premature physical completion");
    const auto publication_delta=raw.completion>sparse.completion?raw.completion-sparse.completion:sparse.completion-raw.completion;
    check(publication_delta<=geometry.fft_samples+211,
        "packed null skip must preserve next-poll completion within one fixed FFT/poll boundary");
    check(sparse.work.scheduled_null_samples>0,"packed fixture must exercise known null fast-forward");
    auto diagnostic=c;diagnostic.scramble=false;
    check(!modem::pattern_amplitude_geometry(diagnostic,search(diagnostic)).supported,
        "shared-envelope legacy public diagnostic must not silently enter amplitude acquisition");
}
void null_is_not_absence_and_rejected_skip() {
    auto c=configuration(100);Fixture fixture(c);const auto s=search(c);
    modem::PatternCorrelator receiver(c,s,16*1024*1024,{.amplitude_only=true});
    check(!receiver.try_skip_unobserved(1),"active source sample must not be discarded");
    const auto cert=receiver.scheduled_capture_envelope(0);check(cert.has_value(),"scheduled capture envelope must be available");
    check(cert->lower.distance(fixture.origin)==0 && cert->upper.distance(fixture.origin)==0,
        "exact arrival lane envelope must keep precise physical UTC sample origin");
    const auto first=fixture.boundary(1);std::array<float,127> pcm{};
    bool admitted=false;const Bytes payload{1};
    for(std::uint64_t at=0;at<first+128;) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),first+128-at));
        for(std::size_t i=0;i<n;++i)pcm[i]=fixture.sample(at+i,payload);
        receiver.push(std::span(pcm).first(n));at+=n;
        for(const auto& burst:receiver.take_bursts()){admitted|=!burst.bits.empty();check(!burst.complete,"payload/null prefix must not be completed");}
    }
    check(admitted,"long sparse fixture must admit its fully scored bit");
    receiver.finish();for(const auto& burst:receiver.take_bursts())check(!burst.complete,"EOF during scheduled null must remain pending");
}

void broad_short_profile_and_phase_cancellation() {
    // Preserve the failed broad-G5 production fixture, including its key and
    // channel seed. A geometrically supported64-chip row failed clean full-bank
    // admission on the former half-chip lattice; do not replace its seed.
    transfer::Options options;options.modem=tuning::resolve(3600,55,tuning::PatternMode::auto_pattern,false,{}).config;
    options.timestamp=1800000000;options.search_seconds=2;options.key.emplace(Bytes(32,0x67));
    options.dsp_workspace_bytes=512ULL*1024*1024;options.content_limit=1024*1024;
    options.audio_timing_error_seconds=.05;
    auto& c=options.modem;c.sample_rate=2000;c.bandwidth_hz=16;c.carrier_hz=500;
    c.constellation_bits=1;c.pattern_symbols=true;c.spreading_factor=64;c.integration_seconds=8;
    c.scramble=true;c.dsss=false;c.dsss_factor=1;c.fake_hopping.reset();c.data_key=options.key;
    c.search_arithmetic=modem::SearchArithmetic::default_mode;c.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
    modem::OscillatorSearchConfig oscillator;oscillator.lf={.0001,0};oscillator.rf={.0001,0};c.oscillator_search=oscillator;
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=5;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;plan->audio_error_seconds=.05;plan->ramp_seconds=.01;
    c.scheduled_hopping=plan;modem::SchedHopSourceMap map(*plan,*options.key,c.sample_rate);
    const auto anchor=map.source_to_physical(map.locate(options.timestamp*c.sample_rate).source);
    options.timestamp=anchor.sample/c.sample_rate;c.stream_phase_samples=anchor.sample%c.sample_rate;
    const auto seeded=transfer::seeded_config(options,options.timestamp);
    const auto symbol=modem::symbol_sample_count(seeded),chip=modem::pattern_chip_samples(seeded);
    check(symbol==16000 && chip==250,"failed broad fixture geometry must remain64 original chips");
    auto tx=transfer::binary_transmitter(Bytes{0},options);
    const auto physical_origin=tx->physical_output_origin(),payload=options.timestamp*c.sample_rate+c.stream_phase_samples;
    const auto source=map.locate(payload).source;
    const auto absent_end=map.source_to_physical(source.add_samples(2*symbol),true).sample-physical_origin;
    auto exact=search(seeded);exact.start_offset_seconds=static_cast<double>(payload-physical_origin)/c.sample_rate;
    const auto reference=modem::pattern_amplitude_geometry(seeded,exact);
    check(reference.supported && reference.fft_samples==128,"failed fixture must retain its original exact FFT geometry");
    std::vector<float> pcm(std::max<std::uint64_t>(tx->total_samples(),absent_end+2*reference.fft_samples+16000));
    for(std::size_t at=0;at<tx->total_samples();) {
        const auto n=tx->read(std::span(pcm).subspan(at,std::min<std::size_t>(317,tx->total_samples()-at)));
        check(n!=0,"broad fixture transmitter must reach its declared endpoint");at+=n;
    }
    std::mt19937 rng(8071);std::normal_distribution<float> noise(0,.003f);for(auto& x:pcm)x+=noise(rng);
    const auto hash=[&] {std::uint64_t value=1469598103934665603ULL;
        for(const auto x:pcm){value^=std::bit_cast<std::uint32_t>(x);value*=1099511628211ULL;}return value;};
    const auto original_hash=hash();Bytes wire{0};transfer::xor_binary_bits(wire,options);
    for(const bool broad:{false,true}) {
        auto query=exact;if(broad){query.start_uncertainty_seconds=3;query.search_stream_phases=true;
            *query.start_offset_seconds-=static_cast<double>(seeded.stream_phase_samples)/c.sample_rate;}
        const auto geometry=modem::pattern_amplitude_geometry(seeded,query);
        check(geometry.supported && geometry.fft_samples==128,"broad phase cancellation must avoid an unnecessarily smaller FFT");
        check(geometry.receiver_timing_subdivisions>=2 &&
            !(geometry.receiver_timing_subdivisions&(geometry.receiver_timing_subdivisions-1)),
            "scheduled quarter-chip refinement must retain the preceding binary timing lattice");
        modem::PatternCorrelator receiver(seeded,query,512ULL*1024*1024,{.amplitude_only=true});
        Bytes received;bool complete=false;std::uint64_t published=0;double score=0,threshold=0;
        for(std::size_t at=0;at<pcm.size();) {
            const auto n=std::min<std::size_t>(512,pcm.size()-at);receiver.push(std::span(pcm).subspan(at,n));at+=n;
            for(const auto& event:receiver.take_bursts()) {
                check(!event.missing_slots,"broad phase-cell reception must not fabricate missing bits");
                if(!event.bits.empty()){check(!complete,"completed stream must not gain new payload bits");
                    received.insert(received.end(),event.bits.begin(),event.bits.end());if(!published)published=at;}
                if(event.complete){check(published && published<at && at>=absent_end,
                    "broad pending bit must precede fully observed physical completion");complete=true;}
            }
            for(const auto& evidence:receiver.candidates(8))if(evidence.score>score) {
                score=evidence.score;threshold=evidence.admission_threshold;
            }
        }
        check(received==wire && complete && score>threshold,"unchanged failed broad waveform must admit its bit and physical absence");
        check(hash()==original_hash,"receiver comparisons must use identical original PCM");
    }
    // Exact algebraic check at fractional nodes around source quarters and
    // multiple group boundaries, including full cryptographic start addresses.
    auto query=exact;query.search_stream_phases=true;query.start_uncertainty_seconds=3;
    const auto geometry=modem::pattern_amplitude_geometry(seeded,query);
    const auto width=modem::pattern_amplitude_phase_cell_upper(seeded,geometry,seeded.stream_phase_samples)-seeded.stream_phase_samples;
    check(width>0,"cancellation proof must exercise a non-singleton canonical phase cell");
    bool delayed_end=false;
    for(std::uint64_t index=0;index<8;++index) {
        const auto address=modem::symbol_stream_address(seeded,seeded.stream_epoch,seeded.stream_phase_samples,index);
        const auto end=map.source_to_physical(source.add_samples((index+1)*symbol),true);
        for(const auto delta:{std::uint64_t{0},width/2,width}) {
            const auto shifted_anchor=map.locate(anchor.add_samples(delta));
            check(shifted_anchor.active && shifted_anchor.source.distance(source)==delta,
                "represented source anchor must remain affine inside its active cell");
            const auto shifted=modem::symbol_stream_address(seeded,seeded.stream_epoch,seeded.stream_phase_samples+delta,index);
            check(address.epoch==shifted.epoch && address.ordinal==shifted.ordinal,
                "canonical phase cancellation must preserve independent private stream addresses");
            const auto shifted_end=map.source_to_physical(shifted_anchor.source.add_samples((index+1)*symbol),true);
            delayed_end|=shifted_end.distance(end)>static_cast<long double>(delta);
            for(unsigned quarter=0;quarter<4;++quarter) {
                const auto point=map.source_to_physical(source.add_samples(index*symbol+quarter*symbol/4));
                const auto group=map.locate(point);
                const auto low=group.flat_begin+1,high=group.flat_end-width-2;
                check(low<=high,"proof needs one complete common active-flat phase interval");
                const auto sample=std::clamp(point.sample,low,high);
                for(const auto fraction:{0.L,.375L,.999L}) {
                    const modem::SchedHopCoordinate q{sample,fraction};
                    const auto a=map.locate(q),b=map.locate(q.add_samples(delta));
                    check(a.active && b.active && a.frame_index==b.frame_index,
                        "fractional proof nodes must share a certified affine active group");
                    const auto relative=a.source.distance(source),other=b.source.distance(shifted_anchor.source);
                    check(relative==other && std::floor(4*relative/symbol)==std::floor(4*other/symbol),
                        "canonical phase width must cancel exactly from fractional source/quarter coordinates");
                }
            }
        }
    }
    check(delayed_end,"proof must retain the END inverse discontinuity across a null interval");
    std::cout<<"broad production fixture PCM="<<original_hash<<" (platform noise generator)\n";
}

void short_split_late_utc_phase() {
    // Reuse the independent primitive vector: master00..1f, M10/G2,
    // frame2 begins at hop22 (UTC8.8s). No search for a favorable key/seed.
    auto c=configuration();c.sample_rate=16000;c.carrier_hz=4000;
    c.bandwidth_hz=1000;c.spreading_factor=320;c.integration_seconds=0;
    Bytes key(32);for(unsigned i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(i);
    c.data_key.emplace(key);c.stream_epoch=8;c.stream_phase_samples=12800;
    modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto location=map.locate(c.stream_epoch*c.sample_rate+c.stream_phase_samples);
    const auto symbol=modem::symbol_sample_count(c);
    check(location.active && location.hop==22 && location.physical_begin==8ULL*c.sample_rate+12800,
        "late-start fixture must retain the independent frozen keyed schedule vector");
    check(symbol==10240 && c.stream_phase_samples>symbol && !modem::packed_scheduled_symbols(c),
        "late-start fixture must be a supported non-packed sub-second split symbol");
    check(modem::canonical_phase_extent(c)==c.sample_rate,
        "paused source acquisition must retain the entire physical UTC second");
    auto ordinary=c;ordinary.scheduled_hopping.reset();
    check(modem::canonical_phase_extent(ordinary)==symbol,
        "ordinary public/continuous phase extent must remain unchanged");
    auto broad=search(c);broad.search_stream_phases=true;broad.start_uncertainty_seconds=3;
    broad.start_offset_seconds=-static_cast<double>(c.stream_phase_samples)/c.sample_rate;
    const auto geometry=modem::pattern_amplitude_geometry(c,broad);
    check(geometry.supported && geometry.fft_samples==32,
        "late-start geometry must retain its complete spectral and observation margins");
    const auto step=geometry.receiver_phase_step_samples;
    std::size_t active_phases=0;
    for(std::uint64_t phase=0;phase<modem::canonical_phase_extent(c);phase+=step) {
        if(!map.locate(c.stream_epoch*c.sample_rate+phase).active)continue;
        check(phase>=symbol,"this counterexample must have no active anchor in the old truncated phase domain");
        check(modem::pattern_amplitude_phase_cell_upper(c,geometry,phase)>=phase,
            "scheduled amplitude phase cells must not wrap at a short symbol");
        ++active_phases;
    }
    check(active_phases==5 && modem::pattern_amplitude_phase_cell_upper(c,geometry,15360)==15999 &&
        modem::canonical_phase_cell_upper(c,15360)==15999,
        "all late physical phase cells must extend through the final sample of the UTC second");
    std::size_t broad_hypotheses=0;
    {
        // Complete broad preflight is bounded; executing this new60k-lane bank
        // to full absence is deliberately left unmeasured here. The retained
        // preceding G5 production regression executes its full broad capture.
        modem::PatternCorrelator receiver(c,broad,512ULL*1024*1024,{.amplitude_only=true});
        broad_hypotheses=receiver.work().hypotheses;
        check(broad_hypotheses>0 && receiver.working_bytes()<=512ULL*1024*1024,
            "default six-second prior must retain supported late-start hypotheses within workspace");
    }
    auto qualified=broad;
    qualified.qualified_start_window=modem::PatternStartWindow{-static_cast<long double>(c.stream_phase_samples),1,16};
    Fixture fixture(c);const Bytes expected{1};
    const auto absent=(6ULL*c.sample_rate+symbol-1)/symbol;
    const auto physical_end=fixture.boundary(1+absent);
    const auto capture_end=physical_end+2*geometry.fft_samples+8ULL*c.sample_rate;
    std::vector<float> pcm(capture_end);
    for(std::uint64_t at=0;at<capture_end;++at)pcm[at]=fixture.sample(at,expected);
    modem::PatternCorrelator receiver(c,qualified,512ULL*1024*1024,{.amplitude_only=true});
    check(receiver.work().hypotheses>0 && receiver.work().hypotheses<broad_hypotheses,
        "qualified timing must retain late-start coverage while reducing actual candidate state");
    Bytes received;bool complete=false;std::uint64_t published=0;
    for(std::size_t at=0;at<pcm.size();) {
        const auto n=std::min<std::size_t>(512,pcm.size()-at);receiver.push(std::span(pcm).subspan(at,n));at+=n;
        for(const auto& event:receiver.take_bursts()) {
            check(!event.missing_slots,"late-start null intervals must not create missing payload bits");
            if(!event.bits.empty()) {check(!complete,"completed late-start stream must not gain bits");
                received.insert(received.end(),event.bits.begin(),event.bits.end());if(!published)published=at;}
            if(event.complete) {check(published && published<at && at>=physical_end,
                "late-start bit must publish before six fully observed eligible seconds finish");complete=true;}
        }
    }
    check(received==expected && complete,
        "qualified full-UTC-phase acquisition must receive the original late-start bit and physical absence");
    std::cout<<"late phase broad preflight="<<broad_hypotheses<<" qualified="<<receiver.work().hypotheses
        <<"; broad full reception not measured in this fixture\n";
}

void finite_timing_coverage_is_not_absence() {
    auto c=configuration();c.spreading_factor=512;
    auto guarded=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);
    guarded->group_hops=1;guarded->clock_error_seconds=.1;
    guarded->peer_clock_error_seconds=0;guarded->audio_error_seconds=.1;
    c.scheduled_hopping=std::move(guarded);
    modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto start=map.source_to_physical(map.locate(c.stream_epoch*c.sample_rate).source);
    c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
    auto s=search(c);s.admitted_clock_half_width_ppm=40;
    const auto geometry=modem::pattern_amplitude_geometry(c,s);
    check(geometry.supported && geometry.clock_hypotheses==1,
        "finite-lifetime fixture must initially support its complete clock cell");
    Fixture fixture(c);modem::PatternCorrelator receiver(c,s,16*1024*1024,{.amplitude_only=true});
    const Bytes payload(6,1);const auto end=fixture.boundary(payload.size());
    std::array<float,257> pcm{};Bytes accepted;bool exhausted=false;
    const auto inspect=[&] {
        for(const auto& burst:receiver.take_bursts()) {
            check(!burst.complete,"unavailable timing coverage must not manufacture physical completion");
            check(!burst.missing_slots,"unscorable coverage must not manufacture missing payload bits");
            accepted.insert(accepted.end(),burst.bits.begin(),burst.bits.end());
        }
    };
    try {
        for(std::uint64_t at=0;at<end;) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),end-at));
            for(std::size_t i=0;i<n;++i)pcm[i]=fixture.sample(at+i,payload);
            receiver.push(std::span(pcm).first(n));at+=n;inspect();
        }
    } catch(const Error& error) {
        check(std::string(error.what()).find("scheduled amplitude timing coverage exhausted")!=std::string::npos,
            "finite timing coverage must fail explicitly without an unrelated receiver failure");
        exhausted=true;
    }
    check(!accepted.empty() && std::all_of(accepted.begin(),accepted.end(),[](auto bit){return bit==1;}),
        "finite-lifetime fixture must first admit and publish genuine payload bits");
    check(exhausted,"the original clock-reference budget must not silently restart at each symbol");
    inspect();receiver.finish();inspect();
}
}
int main(){try{geometry_and_sparse_semantics();arithmetic_and_outer_geometry();canonical_cell_end_and_following_bit();long_chip_flat_safe_geometry();packed_short_slots_and_absence();null_is_not_absence_and_rejected_skip();broad_short_profile_and_phase_cancellation();short_split_late_utc_phase();finite_timing_coverage_is_not_absence();
    std::cout<<"scheduled receiver tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
