#include "datapump/live.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/tuning.hpp"
#include "../src/audio_trace.hpp"
#include "../src/sched_capture.hpp"
#include "cpu_work_budget.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint64_t epoch=1800000000;
constexpr std::uint32_t rate=128;
constexpr auto device="controlled Burst capture";
void check(bool condition,const std::string& message) {if(!condition)throw Error(message);}

audio::TimePrediction dated(long double utc,double uncertainty=.002) {
    return {utc,uncertainty,1./rate,audio::TimingQuality::estimated,0};
}
void timestamp_certificates() {
    using live::detail::BurstCaptureTimestamp;
    audio::TimePrediction reference{100,.125,.001,audio::TimingQuality::bounded,0};
    BurstCaptureTimestamp certificate{500,1000,reference};
    auto point=reference;
    check(certificate.accepts(500,1000,100,point),"equal continuing timestamp envelope rejected");
    point.uncertainty_seconds=0;
    for(const auto edge:{99.875L,100.125L}) {
        point.utc_seconds=edge;
        check(certificate.accepts(500,0,edge,point),"inclusive timestamp boundary rejected");
    }
    for(const auto outside:{99.875L-1e-8L,100.125L+1e-8L}) {
        point.utc_seconds=outside;
        check(!certificate.accepts(500,0,outside,point),"timestamp envelope expanded beyond representation allowance");
    }
    point=reference;point.utc_seconds=101;point.uncertainty_seconds=0;
    check(!certificate.accepts(500,0,100,point),"contradictory callback UTC centers authorized capture omission");
    point.utc_seconds=std::numeric_limits<long double>::quiet_NaN();
    check(!certificate.accepts(500,0,100,point),"nonfinite callback timestamp authorized capture omission");
    point=reference;point.quality=audio::TimingQuality::unavailable;
    check(!certificate.accepts(500,1,100,point)&&!certificate.accepts(500,1,100,std::nullopt),
          "missing or unavailable dates authorized capture omission");
    certificate.reference.uncertainty_seconds=.01;
    point=reference;point.uncertainty_seconds=.001;point.seconds_per_frame=.00101;
    check(certificate.accepts(500,0,100,point)&&!certificate.accepts(500,1000,100,point),
          "callback endpoint escaped the original upper timestamp bound");
    point.seconds_per_frame=.00099;
    check(certificate.accepts(500,0,100,point)&&!certificate.accepts(500,1000,100,point),
          "callback endpoint escaped the original lower timestamp bound");
    point.seconds_per_frame=.001;point.uncertainty_seconds=.02;
    check(!certificate.accepts(500,0,100,point),"later callback renewed the original uncertainty budget");
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    certificate={maximum-4,1000,reference};point=reference;point.uncertainty_seconds=0;point.utc_seconds=100.002L;
    check(certificate.accepts(maximum-2,2,100.002L,point)&&
          !certificate.accepts(maximum-2,3,100.002L,point),"capture certificate sample endpoint wrapped");
    certificate={500,1000,reference};point=reference;
    check(!certificate.accepts(499,0,100,point),"callback before the frozen reference accepted");
    certificate.sample_rate=0;
    check(!certificate.accepts(500,0,100,point),"zero-rate timestamp certificate accepted");
    certificate={500,1000,reference};certificate.reference.utc_seconds=-1;point.utc_seconds=-1;
    check(!certificate.accepts(500,0,-1,point),"negative physical UTC authorized capture omission");

    // The provider's 2ms audio radius and the station's GPS phase policy
    // describe different errors. Two valid states of a 10ms station clock
    // may differ by 20ms; that fixed allowance survives bounded corrections
    // without giving each subsequent callback a new 20ms displacement.
    audio::TimePrediction gps{100,.002,.001,audio::TimingQuality::bounded,0};
    gps.system_clock_phase_uncertainty_seconds=.010;
    certificate={500,1000,gps};point=gps;
    const auto relative_phase=2.L*gps.system_clock_phase_uncertainty_seconds;
    for(const auto correction:{-relative_phase,relative_phase}) {
        point.utc_seconds=gps.utc_seconds+correction;
        check(certificate.accepts(500,1000,point.utc_seconds,point),
              "fixed original two-clock GPS phase boundary rejected");
        point.utc_seconds=gps.utc_seconds+correction+(correction<0?-1e-8L:1e-8L);
        check(!certificate.accepts(500,1000,point.utc_seconds,point),
              "callback escaped the original GPS phase policy");
    }
    const std::array<long double,8> corrections{relative_phase,-relative_phase,.005L,-.005L,
                                               relative_phase,0,-relative_phase,.005L};
    for(std::size_t i=0;i<corrections.size();++i) {
        const auto first=500+2000*i;
        point=gps;point.utc_seconds=gps.utc_seconds+2.L*i+corrections[i];
        check(certificate.accepts(first,1000,point.utc_seconds,point),
              "repeated bounded GPS corrections revoked a fixed original policy");
    }
    point=gps;point.utc_seconds=116+relative_phase+.005L;
    check(!certificate.accepts(16500,1000,point.utc_seconds,point),
          "successive GPS corrections renewed the original phase allowance");
    point=gps;point.system_clock_phase_uncertainty_seconds=.011;
    check(!certificate.accepts(500,1000,100,point),
          "later callback widened the original station-clock policy");
    point=gps;point.rate_uncertainty_fraction=.001;
    check(!certificate.accepts(500,100000,100,point),
          "later rate uncertainty escaped the original GPS timestamp envelope");
}

live::Settings settings(bool private_union) {
    live::Settings value;value.device=device;value.transfer.timestamp=0;value.transfer.search_seconds=6;
    value.transfer.clock_sync.reset();value.transfer.audio_timing_error_seconds=.07;
    auto& c=value.transfer.modem;
    c.sample_rate=rate;c.carrier_hz=32;c.bandwidth_hz=16;c.spreading_factor=64;
    c.integration_seconds=8;c.pulse_shaping=private_union;c.symbol_burst={10,private_union?3U:0U};
    c.receiver_strategy=modem::ReceiverStrategy::existing_only;
    c.oscillator_search.emplace();c.oscillator_search->lf={0,0};c.oscillator_search->rf={0,0};
    value.transfer.automatic_receive_profiles=private_union;
    value.transfer.receive_pattern_mode=tuning::PatternMode::auto_pattern;
    value.transfer.receive_targets_db_hz={12,8};
    if(private_union) {
        // TX stays public/None; these are independently loaded receive keys.
        value.receive_keys.emplace_back(Bytes(32,0x68));
        value.receive_keys.emplace_back(Bytes(32,0x67));
    }
    value.content_limit=value.transfer.content_limit=1024*1024;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=64*1024*1024;
    return value;
}
struct CaptureScript {
    std::vector<float> pcm;
    std::string bits="001";
    std::uint64_t utc_origin_samples=0,first_support=0,first_absent_end=0;
    std::uint64_t completion_checkpoint=0,retirement_end=0;
    std::vector<std::uint64_t> support;
    std::atomic<std::size_t> released{0},delivered{0};
    std::atomic<double> now{0};
    std::atomic<double> capture_clock_offset{0};
    std::thread::id capture_thread; // Published before delivered; gate arms only after a drained capture.
};
CaptureScript* capture_script=nullptr;

std::uint64_t private_retirement_end(const live::Settings& value,std::uint64_t last_waveform_end) {
    auto latest=last_waveform_end;
    for(const bool keyed:{false,true}) {
        const auto profiles=tuning::receive_profiles(value.transfer.modem,value.transfer.receive_targets_db_hz,
                                                     value.transfer.receive_pattern_mode,keyed);
        for(const auto& profile:profiles) {
            const auto symbol=modem::symbol_sample_count(profile);
            const auto padding=modem::pattern_pulse_padding_samples(profile);
            const auto chip=modem::pattern_chip_samples(profile);
            check(symbol>=8ULL*rate && symbol<=16ULL*rate && !(symbol&(symbol-1)),
                  "private retirement bound requires this fixture's power-of-two T8/T16 profiles");
            const auto extra=keyed?profile.symbol_burst.random_limit:0;
            const auto minimum=std::max<std::uint64_t>(2*padding+
                (profile.symbol_burst.wait_symbols+extra+1)*symbol,60ULL*rate);
            const auto quantum=30ULL*rate;
            const auto spacing=((minimum+quantum-1)/quantum)*quantum;
            const auto timing=static_cast<std::uint64_t>(std::ceil(2.L*
                (value.transfer.search_seconds+1+value.transfer.audio_timing_error_seconds+.002)*rate));
            // Bound EVERY retained key/profile, including an alternative that
            // fits the last real symbol. It can need its longest random/grid
            // wait and a whole failed symbol before ending. Keep both timing
            // edges, pulse support and the 20s capture guard. For these zero-
            // rate-error power-of-two symbols, a full 2D FFT block plus two
            // chip edges conservatively bounds the scorer's final lookahead.
            const auto fft_guard=2*symbol+2*chip;
            latest=std::max<std::uint64_t>(latest,last_waveform_end+spacing+symbol+
                20ULL*rate+timing+2*padding+fft_guard);
        }
    }
    return latest;
}
void prepare_capture(CaptureScript& capture,const live::Settings& value,bool secret) {
    auto options=value.transfer;
    const bool amplitude=options.modem.receiver_strategy==modem::ReceiverStrategy::amplitude_only;
    if(secret)options.key.emplace(Bytes(32,0x67));
    const auto padding=modem::pattern_pulse_padding_samples(options.modem);
    options.timestamp=epoch+padding/rate;options.modem.stream_phase_samples=padding%rate;
    const auto config=transfer::seeded_config(options,options.timestamp);
    const auto symbol=modem::symbol_sample_count(config);
    check(symbol==8*rate || (config.receiver_strategy==modem::ReceiverStrategy::amplitude_only && symbol==32*rate),
          "Live fixture must score complete configured eight/32-second symbols");
    // Keyed acquisition promises four original symbols in every receive
    // profile. Keep transmitting beyond the slower profile's startup debt so
    // this fixture can observe a genuine common quiet interval afterward.
    if(secret)capture.bits="0010010010";
    Bytes bits;for(const auto bit:capture.bits)bits.push_back(bit-'0');
    capture.support.resize(bits.size()+1);
    auto tx=transfer::binary_transmitter(bits,options);
    capture.utc_origin_samples=tx->physical_output_origin()-25*rate;
    capture.first_support=epoch*rate-capture.utc_origin_samples;
    capture.support[0]=capture.first_support;
    // Independent integer/crypto recurrence pins the physical absence deadline;
    // the transmitter supplies only ordinary PCM to the real Live receiver.
    std::uint64_t start=0;
    for(std::size_t i=0;i+1<capture.support.size();++i) {
        unsigned extra=0;
        if(secret && config.symbol_burst.random_limit) {
            const auto payload=start+padding;
            const auto bytes=config.data_key->stream(StreamPurpose::Fhss,epoch+payload/rate,
                32*(payload%rate),32,StreamDomain::SymbolBurstWaitV1);
            const auto choices=config.symbol_burst.random_limit+1,limit=256U-256U%choices;
            const auto draw=std::find_if(bytes.begin(),bytes.end(),[&](auto b){return b<limit;});
            check(draw!=bytes.end(),"independent wait draw exhausted");extra=*draw%choices;
        }
        const auto minimum=std::max<std::uint64_t>(start+2*padding+(config.symbol_burst.wait_symbols+extra+1)*symbol,
                                                  start+60ULL*rate);
        const auto quantum=30ULL*rate;
        start=((minimum+quantum-1)/quantum)*quantum;
        capture.support[i+1]=capture.first_support+start;
    }
    capture.first_absent_end=capture.support.back()+padding+symbol;
    // Preserve the original true-message completion deadline. Extra noise is
    // solely for other retained calendars and later ordinary rediscovery.
    capture.completion_checkpoint=capture.first_absent_end+40*rate;
    capture.retirement_end=capture.completion_checkpoint;
    if(secret)capture.retirement_end=std::max(capture.retirement_end,private_retirement_end(value,
        capture.support[capture.bits.size()-1]+2*padding+symbol));
    if(amplitude) {
        // Two grid quanta cover refresh/future-admission alignment, with the
        // entire original arrival union, pulse tails, FFT and trailing guard.
        const auto radius=value.transfer.search_seconds+1+value.transfer.audio_timing_error_seconds+.002;
        modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=radius;
        search.hypotheses={{0,0}};search.admitted_frequency_half_width_hz=search.admitted_clock_half_width_ppm=0;
        search.amplitude_start_window=modem::PatternStartWindow{0,1,radius*rate};
        const auto geometry=modem::pattern_amplitude_geometry(config,search);
        check(geometry.supported,"amplitude retirement geometry unavailable");
        capture.retirement_end=std::max(capture.retirement_end,capture.first_absent_end+
            static_cast<std::uint64_t>(std::ceil((60+20+4*radius)*rate))+2*padding+2*geometry.fft_samples);
    }
    // A final delayed flush can publish END at the last supplied sample.
    // Ordinary discovery requires a later real-input callback, independently
    // of the unchanged true-message completion deadline.
    capture.pcm.resize(capture.retirement_end+60ULL*rate);
    check(capture.pcm.size()<200000,"Live fixture exceeded its bounded PCM scope");
    const auto tx_offset=tx->physical_output_origin()-capture.utc_origin_samples;
    check(tx_offset+tx->total_samples()<capture.first_absent_end,"TX unexpectedly reached the absent symbol");
    for(std::size_t at=0;at<tx->total_samples();) {
        const auto count=tx->read(std::span(capture.pcm).subspan(tx_offset+at,tx->total_samples()-at));
        check(count>0,"Live fixture transmitter stalled");at+=count;
    }
    std::mt19937 random(8071);std::normal_distribution<float> noise(0,.001f);
    for(auto& sample:capture.pcm)sample+=noise(random);
    capture.now=static_cast<double>(capture.utc_origin_samples)/rate;
}
struct Observations {
    std::set<std::uint64_t> ids;
    std::set<std::size_t> prefixes;
    std::map<std::uint64_t,live::SignalUpdate> rows;
    std::map<std::uint64_t,std::uint64_t> revisions;
    std::map<std::string,transfer::Received> contents;
    bool allow_revisions=false;
    bool complete=false,content=false,modeled=false,dated=false;
    bool visible_lineage() const {
        if(ids.empty())return false;
        const auto found=rows.find(*ids.begin());
        return found!=rows.end() && found->second.received_bits;
    }
    void inspect(const live::Snapshot& state,const CaptureScript& capture) {
        check(state.error.empty()&&!state.receiver_health.failed(),"Burst Live failed: "+state.error+" / "+state.status);
        check(state.dsp_buffered_bytes<=64*1024*1024,"Burst Live exceeded its workspace");
        const auto captured=state.capture_recorded_samples+state.capture_certified_null_samples;
        check(state.processed_physical_samples<=captured,"processed frontier exceeded actual captured/proven input");
        modeled|=state.clock_window_modeled;dated|=state.receiver_timing.has_value();
        for(const auto& signal:state.signals) {
            if(!signal.received_bits)continue;
            const auto bits=signal.raw_bits.empty()?signal.text:signal.raw_bits;
            const auto earliest=ids.empty()?signal.id:std::min(signal.id,*ids.begin());
            const auto suffix_alias=allow_revisions && signal.id!=earliest;
            // A transient older-cohort row can begin at a later real symbol.
            // Only that non-earliest private alias may match a known proper
            // source suffix; the earliest physical lineage remains exact from
            // bit zero and must eventually supersede every temporary alias.
            const auto source_offset=suffix_alias?capture.bits.find(bits,1):0;
            const auto exact=bits.size()==signal.received_bits && signal.received_bits<=capture.bits.size() &&
                (suffix_alias?source_offset!=std::string::npos:bits==capture.bits.substr(0,signal.received_bits));
            const auto prior_revision=revisions.find(signal.id);
            const auto stale=prior_revision!=revisions.end() && signal.revision<prior_revision->second;
            const auto changed=prior_revision==revisions.end() || signal.revision!=prior_revision->second;
            const auto prior_row=rows.find(signal.id);
            const auto monotone=prior_row==rows.end() || signal.revision!=prior_row->second.revision ||
                                signal.received_bits>=prior_row->second.received_bits;
            if(!stale && prior_row!=rows.end() && signal.revision>prior_row->second.revision)
                contents.erase(prior_row->second.reception_id);
            std::ostringstream superseded;
            for(const auto id:signal.superseded_ids) {
                if(superseded.tellp()>0)superseded<<',';
                superseded<<id;
            }
            // Multiple configured profiles can briefly select separate timing
            // interpretations before stronger evidence explicitly supersedes
            // them. Apply the same revision/withdrawal policy as the GUI and
            // existing Live profile fixtures, while checking every current
            // selected prefix and physical completion below.
            if(!stale) {
                revisions[signal.id]=signal.revision;
                for(const auto id:signal.superseded_ids) {
                    revisions[id]=std::max(revisions[id],signal.revision);
                    const auto obsolete=rows.find(id);
                    if(obsolete!=rows.end())contents.erase(obsolete->second.reception_id);
                    rows.erase(id);
                }
                rows[signal.id]=signal;
            }
            std::ostringstream effective;
            for(const auto& [id,row]:rows) {
                if(effective.tellp()>0)effective<<';';
                effective<<id<<':'<<row.revision<<':'<<row.received_bits<<':'
                         <<(row.raw_bits.empty()?row.text:row.raw_bits)<<':'<<row.complete;
            }
            audio::trace::record("fixture_signal_observed",[&](auto& f) {
                f.add("signal_id",signal.id);f.add("revision",signal.revision);f.add("signal_sequence",signal.sequence);
                f.add("input_revision",state.receiver_input_revision);f.add("captured_samples",captured);
                f.add("processed_physical_samples",state.processed_physical_samples);
                f.add("frequency_hz",signal.frequency_hz);f.add("reception_id",signal.reception_id);
                f.add("received_bits",signal.received_bits);f.add("bits",bits);
                f.add("lineage",suffix_alias?"temporary private source suffix":"earliest source prefix");
                if(source_offset!=std::string::npos)f.add("source_offset",source_offset);
                f.add("complete",signal.complete);f.add("missing",signal.missing_symbols);
                f.add("superseded_ids",superseded.str());f.add("stale_revision",stale);
                f.add("effective_rows",effective.str());
                f.add("family_source","join receiver_signal_commit by signal_id/revision");
            });
            if(changed || !signal.superseded_ids.empty() || signal.complete) {
                std::cerr<<"Burst signal: id="<<signal.id<<" revision="<<signal.revision
                         <<" sequence="<<signal.sequence<<" input_revision="<<state.receiver_input_revision
                         <<" captured="<<captured<<" frontier="<<state.processed_physical_samples
                         <<" frequency="<<signal.frequency_hz<<" bits="<<bits
                         <<" lineage="<<(suffix_alias?"temporary private suffix":"earliest source prefix")
                         <<" received_bits="<<signal.received_bits<<" complete="<<signal.complete
                         <<" missing="<<signal.missing_symbols<<" reception_id="<<signal.reception_id
                         <<" superseded=["<<superseded.str()<<"] stale="<<stale
                         <<" effective=["<<effective.str()<<"] family=receiver_signal_commit(trace)\n";
            }
            ids.insert(signal.id);
            if(allow_revisions && stale)continue;
            check(monotone,"a selected Burst row lost accepted progress within one interpretation revision");
            check(exact && !signal.missing_symbols,
                  "Burst Live published wrong bits, quiet as payload or a shifted slot");
            if(!allow_revisions)check(ids.size()==1,"Burst Live changed its pending row identity");
            prefixes.insert(signal.received_bits);
            if(signal.complete) {
                // A different configured profile has its own fully scored
                // absent slot. Its completed prefix is provisional and can be
                // superseded, as in the existing Live profile fixtures. The
                // actual full payload must still await its physical absence.
                if(!allow_revisions || signal.received_bits==capture.bits.size()) {
                    check(captured>=capture.first_absent_end,"Burst Live manufactured END from planned quiet or EOF");
                    check(signal.received_bits==capture.bits.size(),"Burst Live completed a truncated message");complete=true;
                }
            }
        }
        for(const auto& result:state.received) {
            check(result.stream_complete && result.observed_bits && result.observed_bits<=capture.bits.size() &&
                  !result.missing_symbols,"Burst Live interpreted incomplete or missing-symbol content");
            if(!allow_revisions || result.observed_bits==capture.bits.size())
                check(captured>=capture.first_absent_end && result.observed_bits==capture.bits.size(),
                      "Burst Live interpreted the full payload before physical absence");
            Bytes expected;for(const auto bit:capture.bits.substr(0,result.observed_bits))expected.push_back(bit-'0');
            check(result.raw_bits==expected && (allow_revisions ||
                  (result.short_text_decoded && result.content.message.data==Bytes{'e'})),
                  "Burst Live changed short-message bits or interpretation");content=true;
            std::string id;constexpr char digits[]="0123456789abcdef";
            for(const auto byte:result.content.message.local_id) {
                id+=digits[byte>>4];id+=digits[byte&15];
            }
            contents[id]=result;
        }
    }
};
std::string diagnostics(const live::Snapshot& state,const CaptureScript& capture) {
    std::ostringstream out;
    out<<" delivered="<<capture.delivered.load()<<" released="<<capture.released.load()
       <<" fed="<<state.samples_received<<" frontier="<<state.processed_physical_samples
       <<" recorded="<<state.capture_recorded_samples<<" certified="<<state.capture_certified_null_samples
       <<" queued="<<state.buffered_samples<<'/'<<state.queued_physical_samples
       <<" decoding="<<state.decoding_samples<<'/'<<state.decoding_physical_samples
       <<" resident="<<state.receiver_instances<<" peak="<<state.peak_receiver_instances
       <<" admissions="<<state.receiver_admissions<<" retirements="<<state.receiver_retirements
       <<" bank_builds="<<state.receiver_bank_builds<<" dsp_bytes="<<state.dsp_buffered_bytes
       <<" modeled="<<state.clock_window_modeled<<" timing_fallback="<<state.clock_timing_fallback
       <<" dated="<<state.receiver_timing.has_value()<<" limited="<<state.receiver_health.search_limited
       <<" overruns="<<state.receiver_health.input_overruns<<" dropped="<<state.receiver_health.dropped_samples
       <<" reset="<<state.receiver_health.receiver_reset<<" interrupted="<<state.receiver_health.input_interrupted
       <<" receiver_error="<<state.receiver_error<<" timing_status="<<state.clock_timing_status
       <<" status="<<state.status;
    return out.str();
}
std::uint64_t private_startup_tail(const live::Settings& value,std::uint64_t frozen_observation) {
    auto latest=frozen_observation;
    const auto profiles=tuning::receive_profiles(value.transfer.modem,value.transfer.receive_targets_db_hz,
                                                 value.transfer.receive_pattern_mode,true);
    check(profiles.size()>=2,"private fixture did not resolve its distinct duration profiles");
    for(const auto& profile:profiles) {
        const auto symbol=modem::symbol_sample_count(profile);
        const auto padding=modem::pattern_pulse_padding_samples(profile);
        // Independent worst case over all keys and whole-symbol random draws.
        // One grid quantum covers a cohort admitted just ahead of the freeze;
        // the complete Default arrival/audio/date envelope remains captured.
        const auto quantum=30ULL*rate;
        const auto minimum=std::max<std::uint64_t>(2*padding+
            (profile.symbol_burst.wait_symbols+profile.symbol_burst.random_limit+1)*symbol,60ULL*rate);
        const auto spacing=((minimum+quantum-1)/quantum)*quantum;
        const auto arrival=static_cast<std::uint64_t>(std::ceil(
            (value.transfer.search_seconds+1+value.transfer.audio_timing_error_seconds+.002)*rate));
        constexpr unsigned original_symbols=4;
        latest=std::max<std::uint64_t>(latest,frozen_observation+quantum+arrival+
            (original_symbols-1)*spacing+symbol+padding+20ULL*rate);
    }
    return latest;
}
void hardware_case(bool secret,double clock_offset=0,bool guard_contradiction=false,bool amplitude=false) {
    auto value=settings(secret);
    if(amplitude) {
        value.transfer.modem.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
        value.transfer.modem.integration_seconds=32;value.transfer.modem.pulse_shaping=true;
        value.transfer.modem.bandwidth_hz=16;
        check(modem::pattern_chips_per_symbol(value.transfer.modem)==256,"amplitude fixture needs256 actual chips");
    }
    if(amplitude) {
        modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=7;
        search.hypotheses={{0,0}};search.admitted_frequency_half_width_hz=search.admitted_clock_half_width_ppm=0;
        search.amplitude_start_window=modem::PatternStartWindow{0,1,7*rate};
        const auto geometry=modem::pattern_amplitude_geometry(value.transfer.modem,search);
        check(geometry.supported,"amplitude fixture geometry: "+geometry.reason);
    }
    CaptureScript capture;prepare_capture(capture,value,secret);
    capture.capture_clock_offset=clock_offset;capture.now=capture.now.load()+clock_offset;
    capture_script=&capture;
    struct Reset {~Reset(){capture_script=nullptr;}} reset;
    const auto main_thread=std::this_thread::get_id();
    std::atomic<bool> decoder_armed{false},decoder_open{false};
    std::atomic<std::uint64_t> decoder_arrivals{0},decoder_permits{0};
    live::Session session([&]{return capture.now.load();},[&] {
        if(decoder_armed.load() && std::this_thread::get_id()!=main_thread &&
           std::this_thread::get_id()!=capture.capture_thread) {
            const auto ticket=decoder_arrivals.fetch_add(1)+1;
            while(!decoder_open.load() && decoder_permits.load()<ticket)
                std::this_thread::sleep_for(1ms);
        }
        return std::chrono::steady_clock::now();
    });
    // Destroy before Session, so assertion unwinding cannot strand its worker.
    struct ReleaseDecoder {std::atomic<bool>& open;~ReleaseDecoder(){open=true;}} release_decoder{decoder_open};
    session.start(value);
    Observations observed;observed.allow_revisions=secret;
    testing::CpuWorkBudget budget(25s,secret?"private Burst Live union":"public Burst Live");
    const auto advance=[&](std::uint64_t target) {
        check(target<=capture.pcm.size(),"capture gate exceeded fixture");capture.released=static_cast<std::size_t>(target);
        while(budget.pending()) {
            const auto state=session.snapshot();
            try {observed.inspect(state,capture);}
            catch(...) {std::cerr<<"Burst observation failed: target="<<target<<diagnostics(state,capture)<<'\n';throw;}
            if(capture.delivered>=target && state.samples_received>=target &&
               !state.buffered_samples&&!state.decoding_samples &&
               !state.queued_physical_samples&&!state.decoding_physical_samples &&
               state.processed_physical_samples>=target)return state;
            std::this_thread::sleep_for(1ms);
        }
        const auto state=session.snapshot();
        throw Error("Burst Live capture did not drain: target="+std::to_string(target)+diagnostics(state,capture));
    };
    const auto capture_only=[&](std::uint64_t target) {
        check(target<=capture.pcm.size(),"capture-only gate exceeded fixture");
        capture.released=static_cast<std::size_t>(target);
        while(budget.pending()) {
            const auto state=session.snapshot();observed.inspect(state,capture);
            if(capture.delivered>=target && state.samples_received>=target)return state;
            std::this_thread::sleep_for(1ms);
        }
        throw Error("independent quiet capture did not reach its gate"+diagnostics(session.snapshot(),capture));
    };
    const auto step_until=[&](auto predicate) {
        while(budget.pending()) {
            const auto state=session.snapshot();observed.inspect(state,capture);
            const auto arrived=decoder_arrivals.load();
            if(arrived>decoder_permits.load()) {
                if(predicate(state))return state; // Worker is actually paused here.
                // Eight ReplayClock calls per quantum keep the existing25s CPU
                // budget bounded. Near the second-symbol guard these are real
                // six-sample PCM blocks; a quantum cannot drain the10s reserve.
                decoder_permits=arrived+7;
            }
            std::this_thread::sleep_for(1ms);
        }
        throw Error("stepped quiet decoder did not reach its gate"+diagnostics(session.snapshot(),capture));
    };
    std::optional<live::Snapshot> backlog_tracked;
    const auto seeking=advance(capture.first_support+4*rate);
    check(observed.prefixes.empty()&&seeking.capture_certified_null_samples==0,
          "cold acquisition skipped input or published a partial first symbol");
    if(guard_contradiction) {
        const auto guarded=advance(capture.first_support+24*rate);
        check(observed.prefixes==std::set<std::size_t>{1}&&guarded.capture_certified_null_samples==0,
              "guard contradiction fixture did not reach a tracked, fully captured post-symbol guard");
        // No quiet proof exists at this point. A later contradictory provider
        // date must still revoke the ORIGINAL frozen timestamp anchor; it
        // cannot establish a fresh anchor and authorize omission after the guard.
        capture.capture_clock_offset=clock_offset+2;
    }
    // Give the real decoder a completed captured guard before releasing the
    // next batch of callbacks. Proof is prospective and cannot retroactively
    // omit samples that capture already queued while scoring was in flight.
    const auto startup=advance(capture.first_support+(amplitude?64:44)*rate);
    std::cerr<<"Burst captured guard: secret="<<secret<<diagnostics(startup,capture)<<'\n';
    check(observed.prefixes==std::set<std::size_t>{1}&&!observed.complete,
          "first complete Burst symbol did not reach its pending row during the gap");
    std::uint64_t quiet_start=0;
    if(secret) {
        const auto startup_tail=private_startup_tail(value,capture.first_support+44*rate);
        const auto padding=modem::pattern_pulse_padding_samples(value.transfer.modem);
        bool found=false;
        for(std::size_t bit=1;bit<capture.bits.size();++bit) {
            const auto left=std::max(capture.support[bit]+padding+8ULL*rate+28ULL*rate,startup_tail+4ULL*rate);
            const auto right=capture.support[bit+1]-28ULL*rate;
            if(left+6ULL*rate<right) {quiet_start=left;found=true;break;}
        }
        check(found,"private payload did not outlast its independently bounded startup promises");
        // Preserve existing profile arbitration: one T16 symbol ties two T8
        // symbols in scored support, so the established T16 interpretation may
        // retain a one-bit selected row until a third T8 symbol wins. Direct
        // receiver tests cover each accepted bit; this ambiguous bank checks
        // exact visible progress and its final complete earliest-ID lineage.
        // Alternative profiles can provisionally complete a shorter prefix
        // after their own absent slot; later revisions withdraw that result.
        for(std::size_t bit=1;bit<capture.bits.size() && capture.support[bit]+40*rate<=quiet_start-4*rate;++bit) {
            advance(capture.support[bit]+40*rate);
            check(observed.visible_lineage()&&!observed.complete,
                  "key/profile startup debt lost its valid lineage or prematurely completed the full payload");
        }
        advance(quiet_start-4*rate);
    } else {
        // Fs128's ordinary 2048-sample plot/admission horizon can admit the
        // B+30 cohort before the first B..B+8 symbol freezes discovery. Preserve
        // that complete original promise, then measure quiet in the SECOND
        // payload gap after draining its complete symbol and guard.
        quiet_start=capture.support[1]+(amplitude?70:50)*rate;
        if(clock_offset==0 && !guard_contradiction && !amplitude) {
            // Queue genuine second-symbol PCM before a later quiet proof exists.
            // Capture reaches the quiet region independently of the parked CPU.
            decoder_armed=true;
            capture_only(quiet_start);
            const auto queued=step_until([&](const auto& state) {
                return state.buffered_samples>0;
            });
            check(observed.prefixes==std::set<std::size_t>{1} && !observed.complete &&
                  queued.processed_physical_samples<capture.support[1] &&
                  queued.capture_recorded_samples>=startup.capture_recorded_samples+8*rate,
                  "quiet backlog probe did not queue an undecoded real second symbol");
            const auto scored=step_until([&](const auto& state) {
                return state.processed_physical_samples>=capture.support[1]+40*rate &&
                       observed.prefixes.contains(2) && state.buffered_samples>0;
            });
            check(capture.delivered==quiet_start && !observed.complete,
                  "queued symbol was not decoded while capture remained in quiet");
            // Hold the decoder with real PCM still queued. A NEW dated callback
            // must omit PCM under the prospective proof, not a duty-cycle model.
            const auto certified=capture_only(quiet_start+rate/2);
            check(certified.capture_certified_null_samples>scored.capture_certified_null_samples &&
                  certified.capture_recorded_samples==scored.capture_recorded_samples &&
                  certified.buffered_samples>0 &&
                  certified.processed_physical_samples==scored.processed_physical_samples,
                  "quiet callbacks did not omit PCM while the real decoder had queued work");
            // Release new capture BEFORE stepping CPU again; capture_only never
            // waits for the decoder. Both threads progress inside the same quiet
            // interval, without making the probe depend on their relative speed.
            capture.released=quiet_start+2*rate;
            step_until([&](const auto& state) {
                return state.processed_physical_samples>=capture.support[1]+45*rate &&
                       state.buffered_samples>0;
            });
            const auto progressing=capture_only(quiet_start+2*rate);
            const auto pcm=[](const auto& state){return state.buffered_samples+state.decoding_samples;};
            check(progressing.samples_received>certified.samples_received &&
                  progressing.capture_certified_null_samples>certified.capture_certified_null_samples &&
                  progressing.capture_recorded_samples==certified.capture_recorded_samples &&
                  pcm(progressing)>0 && pcm(progressing)<pcm(certified) &&
                  progressing.processed_physical_samples>certified.processed_physical_samples &&
                  progressing.receiver_admissions==certified.receiver_admissions &&
                  observed.prefixes==std::set<std::size_t>({1,2}) && !observed.complete,
                  "independent quiet capture did not progress while actual queued PCM drained");
            backlog_tracked=progressing;
            decoder_open=true;
        } else {
            advance(capture.support[1]+40*rate);
            check(observed.prefixes==std::set<std::size_t>({1,2})&&!observed.complete,
                  "public startup promises lost the second exact pending prefix");
        }
    }
    // Capture may already have queued this whole range before amplitude
    // scoring publishes its proof. Omission is prospective: drain the retained
    // tail, then deliver fresh dated callbacks inside the proven interval.
    if(amplitude)advance(quiet_start);
    const auto tracked=backlog_tracked?std::move(*backlog_tracked):advance(quiet_start+(amplitude?2:0)*rate);
    std::cerr<<"Burst quiet checkpoint: secret="<<secret<<" support_seconds="
             <<double(quiet_start-capture.first_support)/rate<<diagnostics(tracked,capture)<<'\n';
    check(observed.prefixes.contains(1)&&!observed.complete,
          "tracked Burst pending row disappeared or completed during the gap");
    if(guard_contradiction)
        check(tracked.capture_certified_null_samples==0,"guard date contradiction was rebased into a new quiet certificate");
    else check(tracked.capture_certified_null_samples>=rate,
               "tracked Live bank did not omit actual PCM during certified silence"+diagnostics(tracked,capture));
    const auto quiet=advance(quiet_start+6*rate);
    check(quiet.receiver_admissions==tracked.receiver_admissions &&
          (guard_contradiction?quiet.capture_certified_null_samples==0:
              quiet.capture_certified_null_samples>tracked.capture_certified_null_samples) &&
          quiet.processed_physical_samples>=quiet_start+6*rate &&
          !quiet.queued_physical_samples&&!quiet.decoding_physical_samples && !observed.complete,
          "frozen bank replenished unrelated starts, failed to drain quiet coordinates or ended during a gap"+diagnostics(quiet,capture));
    for(std::size_t bit=1;bit<capture.bits.size();++bit) {
        if(capture.support[bit]+40*rate<=quiet_start+6*rate)continue;
        advance(capture.support[bit]+40*rate);
        check((secret?observed.visible_lineage():observed.prefixes.contains(bit+1))&&!observed.complete,
              "subsequent complete symbol did not publish its next exact pending prefix");
    }
    advance(capture.first_absent_end-4*rate);
    check(!observed.complete,"partial failed symbol manufactured physical END");
    auto final=advance(capture.completion_checkpoint);
    std::uint64_t discovery_admissions=quiet.receiver_admissions;
    check(observed.complete&&!observed.ids.empty(),
          "true payload did not physically complete by its original deadline");
    const auto completed=observed.rows.find(*observed.ids.begin());
    check(completed!=observed.rows.end() && completed->second.complete &&
          completed->second.received_bits==capture.bits.size() && !completed->second.missing_symbols &&
          (completed->second.raw_bits.empty()?completed->second.text:completed->second.raw_bits)==capture.bits,
          "extra profile-retirement noise concealed a delayed or incorrect true-message END");
    if(secret||amplitude) {
        const auto retired=advance(capture.retirement_end);
        std::cerr<<"Burst retained-calendar fence: seconds="<<double(capture.retirement_end-capture.first_support)/rate
                 <<diagnostics(retired,capture)<<'\n';
        discovery_admissions=retired.receiver_admissions;
    }
    // This is new real input after the frozen cohort/queued-null fence.
    // Two UTC grid quanta let ordinary acquisition admit a fresh first C.
    // Public rolling discovery may already have rebuilt its one receiver;
    // retain its quiet-checkpoint baseline rather than requiring a second one.
    final=advance(capture.pcm.size());
    std::set<std::size_t> expected_prefixes;for(std::size_t n=1;n<=capture.bits.size();++n)expected_prefixes.insert(n);
    // Continuous public acquisition has no bounded arrival window to expose
    // as receiver_timing. Capture omission still requires the actual dated
    // provider, as proved by the null counts and contradiction case above.
    check(observed.complete&&(secret || observed.content)&&(!secret || observed.dated)&&
          (secret || observed.prefixes==expected_prefixes),
          "Default-timed Burst Live did not preserve progress and absence-only completion");
    check(observed.rows.size()==1&&!observed.ids.empty()&&
          observed.rows.begin()->first==*observed.ids.begin(),
          "Burst profile arbitration failed to preserve one final row with the earliest physical identity");
    const auto& final_row=observed.rows.begin()->second;
    check(final_row.complete&&final_row.received_bits==capture.bits.size()&&!final_row.missing_symbols&&
          (final_row.raw_bits.empty()?final_row.text:final_row.raw_bits)==capture.bits,
          "final effective Burst row lost exact bits or physical completion");
    check(observed.contents.size()==(final_row.reception_id.empty()?0U:1U),
          "Burst profile revision retained superseded received content");
    if(!observed.contents.empty()) {
        const auto& [id,result]=*observed.contents.begin();
        Bytes expected;for(const auto bit:capture.bits)expected.push_back(bit-'0');
        check(id==final_row.reception_id && result.stream_complete && result.observed_bits==capture.bits.size() &&
              result.raw_bits==expected && !result.missing_symbols,
              "effective received content disagreed with the final exact Burst row");
    }
    check(final.receiver_admissions>discovery_admissions,"Burst Live did not restore ordinary discovery after END");
    check(final.capture_recorded_samples+final.capture_certified_null_samples==capture.pcm.size(),
          "Burst Live capture disposition lost physical coordinates");
    if(guard_contradiction)check(final.capture_certified_null_samples==0,
        "a later capture renewed quiet permission after the frozen timestamp contradiction");
    if(secret)check(final.peak_receiver_instances>=3,"private union fixture did not admit its distinct receive families");
    session.stop();
    std::cout<<(secret?"Private/key/profile union":"Public")<<" Burst Live: "
        <<final.capture_certified_null_samples<<" certified quiet samples, clock offset "<<clock_offset
        <<" s, guard contradiction "<<guard_contradiction<<", physical END and discovery resumed\n";
}
void simulation_case() {
    auto value=settings(false);value.simulation=true;value.simulation_snr_db=40;
    value.simulation_clock_error_ppm=value.simulation_frequency_offset_hz=value.simulation_phase_noise_degrees_per_sqrt_second=0;
    // Seven seconds past a UTC grid point. RX starts on its independent input
    // clock; the lazy transmitter must wait through media before its next grid.
    live::Session session([]{return static_cast<double>(epoch)+7;});session.start(value);
    session.transmit_bits(Bytes{0,0,1});
    testing::CpuWorkBudget budget(25s,"independent-clock Burst simulation");bool complete=false;
    while(budget.pending()) {
        const auto state=session.snapshot();
        check(state.error.empty()&&!state.receiver_health.failed(),"Burst simulation failed: "+state.error);
        for(const auto& result:state.received) {
            check(result.stream_complete&&result.raw_bits==Bytes({0,0,1})&&result.short_text_decoded,
                  "Burst simulation changed independent-clock receive bits");
            // Earliest core starts at epoch+30. Wait10 and T8 place subsequent
            // starts at+120,+210 and the absent core at+300..308. RX's initial
            // epoch+7 clock therefore requires at least301 seconds of input.
            check(state.virtual_seconds>=301,"simulation aligned RX to TX or omitted initial calendar-wait media");
            complete=true;
        }
        if(complete)break;
        std::this_thread::sleep_for(1ms);
    }
    check(complete,"Burst simulation did not physically complete on its independent clock");session.stop();
}
} // namespace

// Replace only the hardware boundary, as in test_live_profiles. Session's real
// asynchronous queue, complete receiver bank and progress arbitration remain.
namespace datapump::audio {
std::vector<Device> devices(){return {{device,"deterministic Burst input"}};}
void capture(std::uint32_t fs,const std::string& selected,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format,Options options) {
    validate_options(options);
    check(capture_script && selected==device && fs==rate,"unexpected Burst fixture hardware capture");
    auto& script=*capture_script;
    script.capture_thread=std::this_thread::get_id();
    check(bool(options.capture_timing),"Default Burst capture did not request provider timestamps");
    if(format)format({rate,rate,rate/2.,0,TimingQuality::estimated,.002});
    while(!stop.stop_requested()) {
        const auto first=script.delivered.load(),end=std::min(script.released.load(),first+rate/20);
        if(end==first) {std::this_thread::sleep_for(1ms);continue;}
        const auto utc=(static_cast<long double>(script.utc_origin_samples)+first)/rate+
            script.capture_clock_offset.load();
        script.now=static_cast<double>(utc);options.capture_timing(dated(utc));
        const auto samples=std::span(script.pcm).subspan(first,end-first);
        if(options.capture_monitor)options.capture_monitor(samples,rate);
        if(!callback(samples))return;
        script.delivered=end;std::this_thread::yield();
    }
}
void capture(std::uint32_t fs,const std::string& selected,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format) {capture(fs,selected,callback,stop,std::move(format),{});}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,
              ChannelMode,Options) {throw Error("Burst receive fixture unexpectedly played audio");}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,
              ChannelMode) {throw Error("Burst receive fixture unexpectedly played audio");}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,
          ChannelMode) {throw Error("Burst receive fixture unexpectedly played audio");}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,
                          StreamFormatCallback) {throw Error("Burst receive fixture unexpectedly recorded audio");}
} // namespace datapump::audio

int main(int argc,char** argv) {
    try {
        std::string_view selector;
        std::optional<std::filesystem::path> trace_path;
        for(int i=1;i<argc;++i) {
            const std::string_view argument=argv[i];
            if(argument.starts_with("--trace=")) {
                check(!trace_path,"duplicate Burst fixture trace option");
                trace_path=std::filesystem::path(std::string(argument.substr(8)));
                check(trace_path->is_absolute(),"Burst fixture trace path must be absolute");
            } else {
                check(selector.empty(),"multiple Burst fixture selectors");
                check(argument=="--timestamps" || argument=="--simulation" || argument=="--private" ||
                      argument=="--early" || argument=="--guard-contradiction" || argument=="--public" || argument=="--amplitude",
                      "unknown Burst fixture option");
                selector=argument;
            }
        }
        std::optional<audio::trace::Scope> trace;
        if(trace_path)trace.emplace(*trace_path);
        timestamp_certificates();
        if(selector=="--timestamps")return 0;
        if(selector=="--simulation") {simulation_case();return 0;}
        if(selector=="--private") {hardware_case(true);return 0;}
        if(selector=="--amplitude") {hardware_case(false,0,false,true);return 0;}
        if(selector=="--early") {hardware_case(false,-7);return 0;}
        if(selector=="--guard-contradiction") {hardware_case(false,0,true);return 0;}
        hardware_case(false);
        if(selector=="--public")return 0;
        hardware_case(true);hardware_case(false,-7);hardware_case(false,0,true);
        hardware_case(false,0,false,true);simulation_case();
        std::cout<<"Symbol_Burst Live integration passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
