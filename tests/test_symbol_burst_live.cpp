#include "datapump/live.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/tuning.hpp"
#include "../src/audio_trace.hpp"
#include "../src/sched_capture.hpp"
#include "cpu_work_budget.hpp"
#include "paced_capture_probe.hpp"
#include "../src/live_mutex_probe.hpp"
#include "../src/live_snapshot_probe.hpp"
#include "snapshot_holder_os_probe.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstddef>
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
    explicit CaptureScript(bool probe=false):callback_probe(probe){}
    paced_callback_probe::Probe callback_probe;
    std::vector<float> pcm;
    std::string bits="001";
    std::uint64_t utc_origin_samples=0,first_support=0,first_absent_end=0;
    std::uint64_t completion_checkpoint=0,retirement_end=0;
    std::vector<std::uint64_t> support;
    std::atomic<std::size_t> released{0},delivered{0};
    std::atomic<double> now{0};
    std::atomic<double> capture_clock_offset{0};
    std::thread::id capture_thread; // Published before delivered; gate arms only after a drained capture.
    bool paced=false;
    std::atomic<bool> pacing_valid{true},pacing_aborted{false};
    std::atomic<unsigned> pacing_abort_reason{0};
    std::string pacing_callback_error; // Provider-only writer; read after Session joins.
    std::atomic<std::uint64_t> pacing_late_callbacks{0},pacing_max_lateness_ns{0},pacing_callbacks{0};
    std::atomic<std::uint64_t> pacing_min_interval_ns{0},pacing_max_interval_ns{0},pacing_interval_total_ns{0};
    std::atomic<std::uint64_t> pacing_max_duration_ns{0},pacing_duration_total_ns{0},pacing_delivery_ns{0};
    std::chrono::steady_clock::time_point pacing_anchor{};
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
// The optional paced diagnostic does not change the established controlled-clock cases.
struct PacedObservation {
    std::set<std::uint64_t> ids;
    std::map<std::uint64_t,live::SignalUpdate> rows;
    std::map<std::uint64_t,std::uint64_t> revisions;
    std::map<std::string,transfer::Received> contents;
    bool complete=false,diagnostic_follow=false;
    std::size_t observation_failures=0;
    std::string first_observation_failure;
    void require(bool okay,const std::string& message) {
        if(okay)return;
        if(!diagnostic_follow)check(false,message);
        ++observation_failures;
        if(first_observation_failure.empty())first_observation_failure=message;
    }
    void inspect(const live::Snapshot& state,const CaptureScript& capture,bool keyed) {
        check(state.error.empty()&&!state.receiver_health.failed(),"paced receiver health: "+state.error);
        check(state.dsp_buffered_bytes<=64*1024*1024,"paced workspace exceeds 64 MiB");
        const auto ingress=state.capture_recorded_samples+state.capture_certified_null_samples;
        for(const auto& signal:state.signals) {
            if(!signal.received_bits)continue;
            const auto prior=revisions.find(signal.id);
            if(prior!=revisions.end()&&signal.revision<prior->second)continue;
            const auto earliest=ids.empty()?signal.id:std::min(signal.id,*ids.begin());
            const auto bits=signal.raw_bits.empty()?signal.text:signal.raw_bits;
            const bool alias=keyed&&signal.id!=earliest;
            const bool exact=bits.size()==signal.received_bits&&signal.received_bits<=capture.bits.size()&&
                  !signal.missing_symbols&&(alias?capture.bits.find(bits,1)!=std::string::npos:
                  bits==capture.bits.substr(0,signal.received_bits));
            if(!exact) {
                // One failure census, before cleanup truncates capture. No
                // hot-loop output or change to the original acceptance rule.
                std::ostringstream detail;
                detail<<"paced selected raw prefix differs from source: id="<<signal.id
                    <<" revision="<<signal.revision<<" sequence="<<signal.sequence
                    <<" earliest="<<earliest<<" alias="<<alias<<" bits_prefix="<<bits.substr(0,64)
                    <<" received_bits="<<signal.received_bits<<" complete="<<signal.complete
                    <<" missing="<<signal.missing_symbols<<" reception_id="<<signal.reception_id
                    <<" ingress="<<ingress<<" frontier="<<state.processed_physical_samples
                    <<" input_revision="<<state.receiver_input_revision<<" prior_rows=[";
                std::size_t count=0;
                for(const auto& [id,row]:rows) {
                    if(count++==16){detail<<"truncated;";break;}
                    detail<<id<<':'<<row.revision<<':'<<row.received_bits<<':'
                        <<(row.raw_bits.empty()?row.text:row.raw_bits).substr(0,64)<<':'<<row.complete<<';';
                }
                detail<<"] snapshot_rows=[";
                count=0;for(const auto& row:state.signals) {
                    if(count++==16){detail<<"truncated;";break;}
                    detail<<row.id<<':'<<row.revision<<':'<<row.received_bits<<':'
                        <<(row.raw_bits.empty()?row.text:row.raw_bits).substr(0,64)<<':'<<row.complete<<';';
                }
                detail<<"] superseded=[";count=0;for(const auto id:signal.superseded_ids) {
                    if(count++==64){detail<<"truncated";break;}detail<<id<<',';
                }
                detail<<']';require(false,detail.str());
            }
            const auto previous=rows.find(signal.id);
            if(previous!=rows.end()) {
                require(previous->second.revision!=signal.revision||
                      previous->second.received_bits<=signal.received_bits,"paced same-revision progress regressed");
                if(previous->second.revision<signal.revision)contents.erase(previous->second.reception_id);
            }
            revisions[signal.id]=signal.revision;
            for(const auto id:signal.superseded_ids) {
                revisions[id]=std::max(revisions[id],signal.revision);
                const auto obsolete=rows.find(id);
                if(obsolete!=rows.end())contents.erase(obsolete->second.reception_id);
                rows.erase(id);
            }
            rows[signal.id]=signal;ids.insert(signal.id);
            if(!keyed)require(ids.size()==1,"paced plain reception changed its pending identity");
            if(signal.complete&&(!keyed||signal.received_bits==capture.bits.size())) {
                require(ingress>=capture.first_absent_end&&signal.received_bits==capture.bits.size(),
                      "paced full reception ended before complete physical absence");
                complete=true;
            }
        }
        for(const auto& result:state.received) {
            require(result.stream_complete&&result.observed_bits&&result.observed_bits<=capture.bits.size()&&
                  !result.missing_symbols,"paced content is incomplete or has missing symbols");
            if(!keyed||result.observed_bits==capture.bits.size())
                require(ingress>=capture.first_absent_end&&result.observed_bits==capture.bits.size(),
                      "paced full content preceded physical absence");
            Bytes expected;for(const auto bit:capture.bits.substr(0,result.observed_bits))expected.push_back(bit-'0');
            require(result.raw_bits==expected,"paced interpreted content changed the raw bits");
            std::string id;constexpr char digits[]="0123456789abcdef";
            for(const auto byte:result.content.message.local_id){id+=digits[byte>>4];id+=digits[byte&15];}
            contents[id]=result;
        }
    }
    bool visible()const{return !rows.empty();}
    void finish(const CaptureScript& capture) {
        require(complete&&!ids.empty()&&rows.size()==1&&rows.begin()->first==*ids.begin(),
              "paced final arbitration did not retain the earliest single lineage");
        if(rows.empty())return; // The strict mode already threw; follow mode records the failed final invariant.
        const auto& row=rows.begin()->second;
        require(row.complete&&row.received_bits==capture.bits.size()&&!row.missing_symbols&&
              (row.raw_bits.empty()?row.text:row.raw_bits)==capture.bits,"paced final row is not the exact complete source");
        require(contents.size()==(row.reception_id.empty()?0U:1U),"paced retained superseded content");
        if(!contents.empty()) {
            Bytes expected;for(const auto bit:capture.bits)expected.push_back(bit-'0');
            const auto& [id,result]=*contents.begin();
            require(id==row.reception_id&&result.stream_complete&&result.raw_bits==expected&&
                  result.observed_bits==capture.bits.size(),"paced final content disagrees with its row");
        }
    }
};
std::uint64_t paced_pcm_hash(std::span<const float> pcm) {
    std::uint64_t value=14695981039346656037ULL;
    for(const auto byte:std::as_bytes(pcm)){value^=std::to_integer<unsigned char>(byte);value*=1099511628211ULL;}
    return value;
}
std::string paced_json_string(std::string_view text) {
    std::string result="\"";constexpr char hex[]="0123456789abcdef";
    for(const unsigned char c:text.substr(0,256)) {
        if(c=='"'||c=='\\'){result+='\\';result+=c;}
        else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=c;
    }
    return result+'"';
}
void prepare_paced_capture(CaptureScript& capture,const live::Settings& value,bool keyed,bool on) {
    constexpr std::size_t maximum_pcm=262144;
    capture.bits="0010010010010010";capture.paced=true;
    auto options=value.transfer;if(keyed)options.key.emplace(Bytes(32,0x67));
    const auto padding=modem::pattern_pulse_padding_samples(options.modem);
    options.timestamp=epoch+padding/rate;options.modem.stream_phase_samples=padding%rate;
    const auto config=transfer::seeded_config(options,options.timestamp);
    const auto symbol=modem::symbol_sample_count(config);
    check(symbol==8ULL*rate,"paced source must retain T8");
    Bytes bits;for(const auto bit:capture.bits)bits.push_back(bit-'0');
    auto tx=transfer::binary_transmitter(bits,options);
    check(tx->physical_output_origin()>=25ULL*rate,"paced capture origin underflow");
    capture.utc_origin_samples=(epoch-60ULL)*rate; // Identical UTC/noise origin in each pair.
    check(tx->physical_output_origin()>=capture.utc_origin_samples+25ULL*rate,"paced leading observation guard is insufficient");
    capture.first_support=epoch*rate-capture.utc_origin_samples;
    capture.support.resize(bits.size()+1);capture.support[0]=capture.first_support;
    std::uint64_t start=0;
    for(std::size_t i=0;i<bits.size();++i) {
        if(on) {
            const auto minimum=std::max<std::uint64_t>(start+2*padding+11*symbol,start+60ULL*rate);
            const auto absolute=epoch*rate+minimum;constexpr auto quantum=30ULL*rate;
            start=((absolute+quantum-1)/quantum)*quantum-epoch*rate;
        } else start+=symbol;
        capture.support[i+1]=capture.first_support+start;
    }
    capture.first_absent_end=capture.support.back()+padding+symbol;
    capture.completion_checkpoint=capture.first_absent_end+40ULL*rate;
    capture.retirement_end=capture.completion_checkpoint;
    if(keyed) {
        const auto last_end=capture.support[bits.size()-1]+2*padding+symbol;
        capture.retirement_end=std::max(capture.retirement_end,private_retirement_end(value,last_end));
        // Keep the original four-symbol promises, not just the true source's first absent slot.
        capture.retirement_end=std::max<std::uint64_t>(capture.retirement_end,
            private_startup_tail(value,capture.first_support+44ULL*rate)+4ULL*rate);
    }
    auto extent=capture.retirement_end+60ULL*rate;
    extent=(extent+15)/16*16;
    check(extent<=maximum_pcm,"paced original promises exceed 262144-float bound");
    capture.pcm.resize(static_cast<std::size_t>(extent));
    const auto offset=tx->physical_output_origin()-capture.utc_origin_samples;
    check(offset+tx->total_samples()<capture.first_absent_end,"paced waveform reaches its absent slot");
    for(std::size_t at=0;at<tx->total_samples();) {
        const auto n=tx->read(std::span(capture.pcm).subspan(offset+at,tx->total_samples()-at));
        check(n>0,"paced transmitter stalled");at+=n;
    }
    // Both calendars use the same physical-coordinate noise process and seed.
    std::mt19937 random(8071);std::normal_distribution<float> noise(0,.001f);
    for(auto& sample:capture.pcm)sample+=noise(random);
    capture.now=static_cast<double>(capture.utc_origin_samples)/rate;
}
struct PacedSample {
    std::uint64_t ingress=0,frontier=0,nulls=0,recorded=0,queued=0,decoding=0,revision=0;
    std::uint64_t before_ns=0,after_ns=0;
    std::size_t pcm=0,bytes=0;
    double backlog=0,oldest=0;
};
struct PacedHold {
    std::uint64_t arm_at=0,next_guard=0,proof_ticket=0,park_enter_ns=0,proof_park_enter_ns=0,release_ns=0;
    bool attempted=false,parked=false,pcm_loaded=false,proof=false,material=false,released=false,drained=false;
    PacedSample arm,first_park,pcm_debt,proof_before,proof_after,release,drain;
};
struct PacedWindow {
    std::uint64_t left=0,right=0;
    bool start=false,end=false,start_valid=false,end_valid=false,material=false,cleared=false;
    PacedSample start_before,start_after,end_before,end_after,debt;
    PacedHold hold;
};
void paced_burst_case(std::string_view selector,bool keyed,bool burst_on,bool diagnostic_follow=false,bool held=false,bool probe=false,bool mutex_probe=false,bool snapshot_probe=false) {
    using Steady=std::chrono::steady_clock;
    const auto wall_start=Steady::now(),deadline=wall_start+570s;
    const auto cpu_start=std::clock();
    live::mutex_probe::Scope mutex_scope(mutex_probe);
    live::snapshot_probe::Scope snapshot_scope(snapshot_probe);
    std::unique_ptr<snapshot_holder_os_probe::Probe> snapshot_os;
    if(snapshot_probe) snapshot_os=std::make_unique<snapshot_holder_os_probe::Probe>(true,[&]() -> std::optional<snapshot_holder_os_probe::Owner> {
        const auto* state=snapshot_scope.state();
        if(!state)return {};
        const auto serial=state->owner_serial.load(std::memory_order_acquire);
        if(!serial)return {};
        const auto tid=state->owner_tid.load(std::memory_order_relaxed);
        const auto acquired=state->owner_acquired_ns.load(std::memory_order_relaxed);
        if(serial!=state->owner_serial.load(std::memory_order_acquire))return {};
        return snapshot_holder_os_probe::Owner{serial,tid,acquired};
    });
    CaptureScript capture(probe);
    struct Reset {~Reset(){capture_script=nullptr;}} reset;
    const auto main_thread=std::this_thread::get_id();
    std::atomic<bool> decoder_armed{false},decoder_open{false},decoder_waiting{false};
    std::atomic<std::uint64_t> decoder_arrivals{0},decoder_permits{0},decoder_park_ns{0};
    std::unique_ptr<live::Session> session;
    // Declared after Session: release on every unwind before it can join a
    // worker parked in ReplayClock. Stop/destruction below release explicitly too.
    struct ReleaseDecoder {std::atomic<bool>& open;~ReleaseDecoder(){open=true;}} release_decoder{decoder_open};
    PacedObservation observed;observed.diagnostic_follow=diagnostic_follow;
    std::array<PacedWindow,16> windows{};
    PacedSample previous,final;
    std::array<PacedSample,32> slow_snapshots{};
    std::size_t slow_snapshot_count=0;
    live::ReceiverHealth final_health;
    std::string final_status,final_receiver_error;
    std::uint64_t final_admissions=0,final_retirements=0;
    std::size_t peak_dsp=0;
    std::uint64_t pcm_hash=0,startup_tail=0,first_visible_ingress=0,material_since=0,freeze_upper_bound=0;
    const char* startup_measurement=burst_on?"inconclusive_no_visible_acquisition":"not_applicable_off";
    bool finished=false,have_previous=false,material_seen=false,startup_debt=false,poststartup_debt=false;
    bool tail_bounded=false;
    std::optional<std::size_t> active_hold;
    std::uint64_t previous_park_ticket=0;
    std::string failure,cleanup_error;
    const auto elapsed_ns=[&](Steady::time_point t){return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t-wall_start).count());};
    try {
        auto value=settings(keyed);value.transfer.modem.symbol_burst={burst_on?10U:0U,0};
        prepare_paced_capture(capture,value,keyed,burst_on);
        pcm_hash=paced_pcm_hash(capture.pcm);
        check(Steady::now()<deadline,"paced preparation exceeded the internal wall deadline");
        const auto padding=modem::pattern_pulse_padding_samples(value.transfer.modem);
        const auto symbol=modem::symbol_sample_count(value.transfer.modem);
        const auto guard=20ULL*rate+static_cast<std::uint64_t>(std::ceil(
            (value.transfer.search_seconds+1+value.transfer.audio_timing_error_seconds+.002)*rate));
        for(std::size_t i=0;i<windows.size();++i) {
            windows[i].left=capture.support[i]+2*padding+symbol+guard;
            windows[i].right=capture.support[i+1]>guard?capture.support[i+1]-guard:0;
            // On: start at source support end, before the mandatory 20-second
            // capture guard, so a proof cannot make the loading stage all nulls.
            // Off assigns its continuous-capture control boundary after
            // actual acquisition becomes visible below.
            windows[i].hold.arm_at=burst_on?capture.support[i]+2*padding+symbol:capture.support[i]+symbol/2;
            windows[i].hold.next_guard=burst_on?windows[i].right:capture.support[i+1];
        }
        capture_script=&capture;
        if(held)session=std::make_unique<live::Session>([&]{return capture.now.load();},[&] {
            // The producer and snapshot caller always retain their real clock.
            // This is the existing hardware_case seam, not a media-clock change.
            if(decoder_armed.load() && std::this_thread::get_id()!=main_thread &&
               std::this_thread::get_id()!=capture.capture_thread) {
                const auto ticket=decoder_arrivals.fetch_add(1)+1;
                if(!decoder_open.load() && decoder_armed.load() && decoder_permits.load()<ticket) {
                    decoder_park_ns=static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now()-wall_start).count());
                    decoder_waiting=true;
                    while(!decoder_open.load() && decoder_armed.load() && decoder_permits.load()<ticket)
                        std::this_thread::sleep_for(1ms);
                    decoder_waiting=false;
                }
            }
            return Steady::now();
        });
        else session=std::make_unique<live::Session>([&]{return capture.now.load();});
        session->start(value); // No readiness wait or decoder gate precedes capture.
        while(Steady::now()<deadline) {
            const auto before=Steady::now();const auto state=session->snapshot();const auto after=Steady::now();
            final_health=state.receiver_health;final_status=state.status;final_receiver_error=state.receiver_error;
            final_admissions=state.receiver_admissions;final_retirements=state.receiver_retirements;
            PacedSample now;
            now.ingress=state.capture_recorded_samples+state.capture_certified_null_samples;
            now.frontier=state.processed_physical_samples;now.nulls=state.capture_certified_null_samples;
            now.recorded=state.capture_recorded_samples;now.queued=state.queued_physical_samples;
            now.decoding=state.decoding_physical_samples;now.revision=state.receiver_input_revision;
            now.pcm=state.buffered_samples+state.decoding_samples;now.bytes=state.dsp_buffered_bytes;
            const bool finite_times=std::isfinite(state.receiver_backlog_seconds)&&std::isfinite(state.receiver_oldest_input_seconds);
            now.backlog=finite_times?state.receiver_backlog_seconds:0;now.oldest=finite_times?state.receiver_oldest_input_seconds:0;
            now.before_ns=elapsed_ns(before);now.after_ns=elapsed_ns(after);final=now;
            if(probe) {
                if(slow_snapshot_count<slow_snapshots.size())slow_snapshots[slow_snapshot_count++]=now;
                else {
                    const auto least=std::min_element(slow_snapshots.begin(),slow_snapshots.end(),[](const auto& a,const auto& b) {
                        return a.after_ns-a.before_ns<b.after_ns-b.before_ns;
                    });
                    if(now.after_ns-now.before_ns>least->after_ns-least->before_ns)*least=now;
                }
            }
            check(finite_times,"paced backlog timing is nonfinite");
            check(now.frontier<=now.ingress&&now.queued+now.decoding==now.ingress-now.frontier,
                  "paced physical queue/frontier reconciliation failed");
            if(have_previous&&now.revision==previous.revision)check(now.ingress>=previous.ingress&&
                now.frontier>=previous.frontier,"paced ingress/frontier regressed within its revision");
            else if(have_previous)check(previous.ingress==0,"paced capture revision changed after input arrived");
            peak_dsp=std::max(peak_dsp,now.bytes);
            observed.inspect(state,capture,keyed);
            if(!first_visible_ingress&&observed.visible())first_visible_ingress=now.ingress;
            // A pending event can be visible before the same feed publishes
            // receiver timing. Retry the first available monotone upper bound.
            if(first_visible_ingress&&burst_on&&!freeze_upper_bound) {
                if(!keyed) {
                    // Public rolling FFT has no modeled receiver_timing. The
                    // captured ingress nevertheless upper-bounds bank.samples
                    // at the first visible commit: enqueue owns the complete
                    // callback before the decoder sees it. Queueing can only
                    // make this fence conservative, never move it behind freeze.
                    freeze_upper_bound=first_visible_ingress;
                    const auto frozen=std::max<std::uint64_t>(freeze_upper_bound,capture.first_support+44ULL*rate);
                    startup_tail=frozen+30ULL*rate+3*symbol+2*padding+guard;
                    tail_bounded=startup_tail<=capture.retirement_end;
                    startup_measurement=tail_bounded?"bounded_public_ingress_upper":
                        "inconclusive_prepared_capture_tail";
                } else if(burst_on&&state.receiver_timing) {
                    // refresh_bank admits at bank.samples, not capture ingress.
                    // For this 16-sample PCM provider, the previously published
                    // processing timestamp can lag the current feed by one block;
                    // its end needs one further block. F may lag both and is not
                    // an upper bound. See live.cpp:3308,3338,3357,3650,4602.
                    const auto ticks=state.receiver_timing->utc_seconds*rate-
                        static_cast<long double>(capture.utc_origin_samples);
                    if(std::isfinite(ticks)&&ticks>=0&&ticks<=capture.pcm.size()) {
                        freeze_upper_bound=static_cast<std::uint64_t>(std::ceil(ticks))+32;
                        const auto frozen=std::max<std::uint64_t>(freeze_upper_bound,capture.first_support+44ULL*rate);
                        startup_tail=keyed?private_startup_tail(value,frozen)+4ULL*rate:
                            frozen+30ULL*rate+3*symbol+2*padding+guard;
                        tail_bounded=startup_tail<=capture.retirement_end;
                        startup_measurement=tail_bounded?"bounded_processing_timestamp":
                            "inconclusive_prepared_capture_tail";
                    } else startup_measurement="inconclusive_processing_timestamp_range";
                } else if(burst_on)startup_measurement="inconclusive_processing_timestamp_unavailable";
            }
            // Require debt beyond an ordinary one-block pipeline, persisting across polls.
            const bool substantial=now.ingress-now.frontier>=2ULL*rate&&now.backlog>=2.&&now.oldest>=.020;
            if(!substantial)material_since=0;
            else if(!material_since)material_since=now.after_ns;
            const bool material=substantial&&now.after_ns>=material_since+10000000ULL;
            material_seen|=material;
            if(material&&(!first_visible_ingress||(burst_on&&(!startup_tail||now.ingress<startup_tail))))startup_debt=true;
            if(material&&first_visible_ingress&&(!burst_on||(startup_tail&&now.ingress>=startup_tail)))poststartup_debt=true;
            const auto park_ticket=decoder_waiting.load()?decoder_arrivals.load():0;
            if(have_previous) {
                std::size_t crossings=0;
                for(const auto& window:windows) {
                    crossings+=previous.ingress<window.left&&now.ingress>=window.left;
                    if(window.left<window.right)crossings+=previous.ingress<window.right&&now.ingress>=window.right;
                }
                const bool narrow=crossings==1&&now.ingress-previous.ingress<=2ULL*rate&&
                    now.after_ns-previous.before_ns<=40000000ULL;
                for(auto& window:windows) {
                    if(!window.start&&previous.ingress<window.left&&now.ingress>=window.left) {
                        window.start=true;window.start_before=previous;window.start_after=now;
                        window.start_valid=narrow&&window.left<window.right&&now.ingress<window.right;
                    }
                    if(!window.end&&window.left<window.right&&previous.ingress<window.right&&now.ingress>=window.right) {
                        window.end=true;window.end_before=previous;window.end_after=now;
                        window.end_valid=narrow&&previous.ingress>=window.left&&previous.ingress<window.right;
                    }
                    const bool current=burst_on&&tail_bounded&&startup_tail&&window.left>=startup_tail&&
                        previous.ingress>=window.left&&now.ingress<window.right;
                    // Prospective proof must be active while genuine queued work remains.
                    if(current&&material&&now.nulls>previous.nulls&&now.recorded==previous.recorded) {
                        if(!window.material){window.material=true;window.debt=now;}
                    }
                    if(current&&window.material&&now.ingress>window.debt.ingress&&
                       now.nulls>window.debt.nulls&&now.recorded==window.debt.recorded&&
                       now.frontier>window.debt.frontier&&now.queued+now.decoding==0&&now.backlog==0)
                        window.cleared=true;
                }
            }
            if(held) {
                for(auto& window:windows) {
                    auto& h=window.hold;
                    if(h.released&&!h.drained&&now.ingress<window.right&&now.ingress>h.release.ingress&&
                       now.recorded==h.release.recorded&&now.nulls>h.release.nulls&&now.pcm==0&&
                       now.frontier>h.release.frontier&&now.queued+now.decoding==0&&now.backlog==0) {
                        h.drained=true;h.drain=now;
                    }
                }
                if(active_hold) {
                    auto& window=windows[*active_hold];auto& h=window.hold;
                    // Reserve two media seconds before the next required window;
                    // a missed bound fails this fixture, rather than extending it.
                    check(!burst_on||now.ingress+2ULL*rate<h.next_guard,
                          "paced held decoder reached the next guarded window before release");
                    if(park_ticket&&!h.parked) {
                        h.parked=true;h.first_park=now;h.park_enter_ns=decoder_park_ns.load();
                        check(burst_on||now.ingress+2ULL*rate<=h.next_guard,
                              "paced Off control parked without room for fresh independently captured PCM");
                    }
                    const auto reserve=burst_on?4ULL*rate:2ULL*rate;
                    if(h.parked&&!h.pcm_loaded&&now.pcm>=reserve&&now.backlog>=double(reserve)/rate&&now.oldest>=.020) {
                        h.pcm_loaded=true;h.pcm_debt=now;
                    }
                    // A ReplayClock call can precede proof publication. Do not
                    // infer permission from a calendar or from old null totals:
                    // fresh dated callbacks must omit input while this exact
                    // ticket is parked, with real PCM still queued and unchanged.
                    if(burst_on&&!h.proof&&h.pcm_loaded&&have_previous&&park_ticket&&
                       park_ticket==previous_park_ticket&&previous.ingress>=window.left&&now.ingress<window.right&&
                       now.nulls>previous.nulls&&now.recorded==previous.recorded&&
                       now.frontier==previous.frontier&&now.pcm==previous.pcm&&now.pcm>=2ULL*rate) {
                        h.proof=true;h.proof_ticket=park_ticket;h.proof_before=previous;h.proof_after=now;
                        h.proof_park_enter_ns=decoder_park_ns.load();
                    }
                    if(h.pcm_loaded&&material&&now.pcm>=2ULL*rate&&(!burst_on||h.proof))h.material=true;
                    const bool aged=h.parked&&now.after_ns>=h.first_park.after_ns+40000000ULL;
                    const bool proven_hold=burst_on?(h.proof&&window.material&&
                        park_ticket==h.proof_ticket&&now.recorded==h.proof_after.recorded&&
                        now.after_ns>=h.proof_after.after_ns+40000000ULL):
                        (park_ticket&&now.frontier==h.first_park.frontier&&
                         now.ingress>=h.first_park.ingress+2ULL*rate&&
                         now.recorded>=h.first_park.recorded+2ULL*rate);
                    if(aged&&proven_hold&&h.material&&now.pcm>=2ULL*rate) {
                        h.released=true;h.release=now;h.release_ns=elapsed_ns(Steady::now());decoder_armed=false;
                        active_hold.reset();
                    } else if(burst_on&&!h.proof&&aged&&park_ticket&&now.pcm>=4ULL*rate) {
                        // At most eight clock calls can commit eight 16-sample
                        // blocks. A four-second PCM reserve leaves at least
                        // three seconds while the next prospective proof is
                        // published. Capture continues at its original pace.
                        decoder_permits=park_ticket+7;
                    }
                } else if(!decoder_waiting.load()&&(!burst_on||!observed.complete)&&observed.visible()) {
                    for(std::size_t i=0;i<windows.size();++i) {
                        auto& window=windows[i];auto& h=window.hold;
                        // Off has no discardable calendar gap. Its control starts
                        // after actual acquisition becomes visible and while the
                        // independent provider still offers original PCM. A slow
                        // private bank can first become visible after all payload
                        // slots were captured; those slots cannot arm this control.
                        const bool after_startup=burst_on?tail_bounded&&startup_tail&&
                            h.arm_at>=startup_tail&&now.frontier>=startup_tail:
                            first_visible_ingress&&now.ingress+4ULL*rate<capture.pcm.size()&&
                            (!i||(windows[i-1].hold.released&&
                                  now.frontier>windows[i-1].hold.release.frontier));
                        const bool room=burst_on?now.ingress>=h.arm_at&&now.ingress+2ULL*rate<window.left:
                            now.ingress+4ULL*rate<capture.pcm.size();
                        if(!h.attempted&&after_startup&&room) {
                            if(!burst_on){h.arm_at=now.ingress;h.next_guard=capture.pcm.size();}
                            h.attempted=true;h.arm=now;active_hold=i;
                            decoder_permits=decoder_arrivals.load();decoder_armed=true;break;
                        }
                    }
                }
            }
            previous_park_ticket=park_ticket;
            previous=now;have_previous=true;
            check(!capture.pacing_aborted.load(),"paced producer stopped: deadline/callback failure; input truncated");
            if(capture.delivered>=capture.pcm.size()&&now.ingress==capture.pcm.size()&&
               now.frontier==now.ingress&&(observed.complete||diagnostic_follow)) {finished=true;break;}
            std::this_thread::sleep_for(held?1ms:5ms);
        }
        check(finished,"paced fixture reached its 570-second deadline before full input, physical END and drain");
        check(final.queued+final.decoding==0&&final.pcm==0&&final.backlog==0,"paced final input did not drain");
        // Unavailable startup-tail evidence makes rescue inconclusive; it is
        // distinct from failure of exact reception/END for the complete input.
        observed.finish(capture);
        if(observed.observation_failures)failure=observed.first_observation_failure;
    } catch(const std::exception& e){failure=e.what();}
      catch(...){failure="unknown paced fixture exception";}
    // Stop and destruction happen before evidence is emitted; retain a prior test failure.
    decoder_open=true;
    try {if(session){session->stop();session.reset();}}
    catch(const std::exception& e){cleanup_error=e.what();}
    catch(...){cleanup_error="unknown cleanup exception";}
    session.reset(); // Also join the provider if explicit stop reported an error.
    if(snapshot_os)snapshot_os->stop();
    const auto elapsed=std::chrono::duration<double>(Steady::now()-wall_start).count();
    if(elapsed>570.&&failure.empty())failure="paced fixture exceeded its 570-second internal wall bound including cleanup";
    if(!cleanup_error.empty()&&failure.empty())failure="paced fixture cleanup failed";
    const bool valid=capture.pacing_valid.load()&&!capture.pacing_aborted.load()&&
        capture.delivered==capture.pcm.size()&&capture.pacing_callbacks.load()>1&&
        capture.pacing_max_lateness_ns.load()<=20000000ULL;
    if(!valid&&failure.empty())failure="paced producer did not deliver complete input at a valid fixed cadence";
    const auto qualifies=[&](const PacedWindow& w){return w.start_valid&&w.end_valid&&w.material&&w.cleared&&
        w.end_before.queued+w.end_before.decoding==0&&w.end_before.backlog==0&&
        w.end_before.recorded==w.debt.recorded&&w.end_before.nulls>w.debt.nulls&&
        (!held||(w.hold.proof&&w.hold.pcm_loaded&&w.hold.material&&w.hold.released&&w.hold.drained&&
                  w.hold.proof_after.pcm>=2ULL*rate&&w.hold.release.pcm>=2ULL*rate));};
    std::size_t cleared=0;for(const auto& window:windows)cleared+=qualifies(window);
    std::size_t held_released=0;for(const auto& window:windows)held_released+=window.hold.released;
    if(held&&failure.empty()&&((burst_on&&cleared<3)||(!burst_on&&held_released<3)))
        failure=burst_on?"paced held On did not demonstrate three actual poststartup PCM gap drains":
                         "paced held Off did not complete three material PCM holds";
    const bool functional=failure.empty();
    const char* classification=!functional||!valid?"no_demonstrated_rescue":
        held&&!burst_on?"control_no_rescue_claim":
        burst_on&&cleared>=3?"observed":!material_seen?"unchallenged":"no_demonstrated_rescue";
    const auto cpu_end=std::clock();
    std::cout<<"PACED_BURST_JSON {\"diagnostic_follow\":"<<(diagnostic_follow?"true":"false")
        <<",\"observation_failures\":"<<observed.observation_failures
        <<",\"final_rows\":[";
    bool first_row=true;
    for(const auto& [id,row]:observed.rows) {
        if(!first_row)std::cout<<',';first_row=false;
        std::cout<<"{\"id\":"<<id<<",\"revision\":"<<row.revision<<",\"received_bits\":"<<row.received_bits
            <<",\"bits\":"<<paced_json_string(row.raw_bits.empty()?row.text:row.raw_bits)
            <<",\"missing\":"<<row.missing_symbols<<",\"complete\":"<<(row.complete?"true":"false")<<'}';
    }
    std::cout<<"],\"selector\":"<<paced_json_string(selector)
        <<",\"configuration\":{\"keyed\":"<<(keyed?"true":"false")<<",\"calendar\":\""<<(burst_on?"N10J0":"N0J0")
        <<"\",\"sample_rate\":128,\"integration_seconds\":8,\"search_seconds\":6,\"workspace_bytes\":67108864,\"pace_factor\":64,\"callback_samples\":16,\"receive_keys\":"<<(keyed?2:0)
        <<",\"automatic_profiles\":"<<(keyed?"true":"false")<<",\"receiver_strategy\":\"existing_only\"}"
        <<",\"logical_bits\":"<<paced_json_string(capture.bits)<<",\"pcm_float_bytes\":"<<sizeof(float)
        <<",\"pcm_samples\":"<<capture.pcm.size()<<",\"pcm_byte_fnv1a64\":\""<<std::hex<<pcm_hash<<std::dec
        <<"\",\"functional\":\""<<(functional?"pass":"fail")<<"\",\"failure\":"<<paced_json_string(failure)
        <<",\"cleanup_error\":"<<paced_json_string(cleanup_error)<<",\"pacing_valid\":"<<(valid?"true":"false")
        <<",\"classification\":\""<<classification<<"\",\"wall_seconds\":"<<elapsed<<",\"cpu_seconds\":"
        <<(cpu_start==static_cast<std::clock_t>(-1)||cpu_end==static_cast<std::clock_t>(-1)?-1.:
            static_cast<double>(cpu_end-cpu_start)/CLOCKS_PER_SEC)
        <<",\"callbacks\":"<<capture.pacing_callbacks.load()<<",\"requested_interval_ns\":1953125"
        <<",\"actual_interval_min_ns\":"<<capture.pacing_min_interval_ns.load()
        <<",\"actual_interval_max_ns\":"<<capture.pacing_max_interval_ns.load()
        <<",\"actual_interval_total_ns\":"<<capture.pacing_interval_total_ns.load()
        <<",\"requested_delivery_span_ns\":"<<(capture.pcm.empty()?0ULL:(capture.pcm.size()/16-1)*1953125ULL)
        <<",\"actual_delivery_span_ns\":"<<capture.pacing_delivery_ns.load()
        <<",\"callback_duration_max_ns\":"<<capture.pacing_max_duration_ns.load()
        <<",\"callback_duration_total_ns\":"<<capture.pacing_duration_total_ns.load()
        <<",\"late_deadline_attempts\":"<<capture.pacing_late_callbacks.load()<<",\"max_lateness_ns\":"<<capture.pacing_max_lateness_ns.load()
        <<",\"producer_aborted\":"<<(capture.pacing_aborted.load()?"true":"false")
        <<",\"producer_abort_reason\":"<<capture.pacing_abort_reason.load()
        <<",\"producer_error\":"<<paced_json_string(capture.pacing_callback_error)
        <<",\"health\":{\"input_overruns\":"<<final_health.input_overruns<<",\"dropped_samples\":"<<final_health.dropped_samples
        <<",\"input_interrupted\":"<<(final_health.input_interrupted?"true":"false")
        <<",\"search_limited\":"<<(final_health.search_limited?"true":"false")
        <<",\"receiver_reset\":"<<(final_health.receiver_reset?"true":"false")<<'}'
        <<",\"status\":"<<paced_json_string(final_status)<<",\"receiver_error\":"<<paced_json_string(final_receiver_error)
        <<",\"admissions\":"<<final_admissions<<",\"retirements\":"<<final_retirements
        <<",\"captured_samples\":"<<final.ingress<<",\"frontier\":"<<final.frontier
        <<",\"queued_physical\":"<<final.queued<<",\"decoding_physical\":"<<final.decoding
        <<",\"sampled_peak_dsp_bytes\":"<<peak_dsp<<",\"first_visible_ingress\":"<<first_visible_ingress
        <<",\"startup_tail\":"<<startup_tail<<",\"prepared_retirement_end\":"<<capture.retirement_end
        <<",\"freeze_upper_bound\":"<<freeze_upper_bound<<",\"startup_measurement\":"<<paced_json_string(startup_measurement)
        <<",\"startup_tail_applicable\":"<<(burst_on?"true":"false")
        <<",\"startup_tail_covered\":"<<(tail_bounded?"true":"false")
        <<",\"startup_material_debt\":"<<(startup_debt?"true":"false")
        <<",\"poststartup_material_debt\":"<<(poststartup_debt?"true":"false")
        <<",\"material_min_pcm_seconds\":2,\"material_min_oldest_wall_seconds\":0.020,\"material_persistence_ns\":10000000"
        <<",\"decoder_hold_enabled\":"<<(held?"true":"false")<<",\"decoder_hold_released\":"<<held_released
        <<",\"decoder_hold_schedule\":"<<paced_json_string(!held?"none":burst_on?"poststartup_guarded_quiet":"visible_acquisition_ongoing_capture_control")
        <<",\"decoder_hold_min_wall_ns\":40000000,\"decoder_hold_pcm_reserve_seconds\":"<<(burst_on?4:2)
        <<",\"cleared_poststartup_gaps\":"<<cleared<<",\"windows\":[";
    const auto sample_json=[](const PacedSample& s) {
        std::cout<<"{\"ingress\":"<<s.ingress<<",\"frontier\":"<<s.frontier<<",\"nulls\":"<<s.nulls
            <<",\"recorded\":"<<s.recorded<<",\"queued\":"<<s.queued<<",\"decoding\":"<<s.decoding
            <<",\"pcm\":"<<s.pcm<<",\"backlog_seconds\":"<<s.backlog<<",\"oldest_seconds\":"<<s.oldest
            <<",\"before_ns\":"<<s.before_ns<<",\"after_ns\":"<<s.after_ns<<'}';
    };
    for(std::size_t i=0;i<windows.size();++i) {
        if(i)std::cout<<',';const auto& w=windows[i];
        std::cout<<"{\"index\":"<<i<<",\"guarded_end\":"<<w.left<<",\"next_guard\":"<<w.right
            <<",\"start_valid\":"<<(w.start_valid?"true":"false")<<",\"end_valid\":"<<(w.end_valid?"true":"false")
            <<",\"material\":"<<(w.material?"true":"false")<<",\"cleared\":"<<(w.cleared?"true":"false")
            <<",\"qualifies\":"<<(qualifies(w)?"true":"false")
            <<",\"start_before\":";sample_json(w.start_before);std::cout<<",\"start_after\":";sample_json(w.start_after);
        std::cout<<",\"end_before\":";sample_json(w.end_before);std::cout<<",\"end_after\":";sample_json(w.end_after);
        std::cout<<",\"quiet_debt\":";sample_json(w.debt);std::cout<<'}';
    }
    std::cout<<"],\"decoder_holds\":[";
    bool first_hold=true;
    for(std::size_t i=0;i<windows.size();++i) {
        const auto& h=windows[i].hold;if(!h.attempted)continue;
        if(!first_hold)std::cout<<',';first_hold=false;
        std::cout<<"{\"index\":"<<i<<",\"arm_at\":"<<h.arm_at<<",\"next_guard\":"<<h.next_guard
            <<",\"phase\":"<<paced_json_string(h.drained?"drained":h.released?"released":h.proof?"holding_proven_quiet":
                h.pcm_loaded?(burst_on?"stepping_to_proof":"holding_control_pcm"):h.parked?"loading_pcm":"awaiting_decoder")
            <<",\"limit_kind\":"<<paced_json_string(burst_on?"next_receive_guard":"prepared_capture_end")
            <<",\"park_enter_ns\":"<<h.park_enter_ns<<",\"proof_park_enter_ns\":"<<h.proof_park_enter_ns<<",\"release_ns\":"<<h.release_ns
            <<",\"parked\":"<<(h.parked?"true":"false")<<",\"pcm_loaded\":"<<(h.pcm_loaded?"true":"false")
            <<",\"proof\":"<<(h.proof?"true":"false")<<",\"proof_ticket\":"<<h.proof_ticket
            <<",\"material\":"<<(h.material?"true":"false")<<",\"released\":"<<(h.released?"true":"false")
            <<",\"drained\":"<<(h.drained?"true":"false")<<",\"arm\":";sample_json(h.arm);
        std::cout<<",\"first_park\":";sample_json(h.first_park);std::cout<<",\"pcm_debt\":";sample_json(h.pcm_debt);
        std::cout<<",\"proof_before\":";sample_json(h.proof_before);std::cout<<",\"proof_after\":";sample_json(h.proof_after);
        std::cout<<",\"release\":";sample_json(h.release);std::cout<<",\"drain\":";sample_json(h.drain);std::cout<<'}';
    }
    std::cout<<"],\"callback_probe\":";capture.callback_probe.emit_json(std::cout);
    std::cout<<",\"mutex_probe\":";live::mutex_probe::emit_json(std::cout,mutex_scope);
    std::cout<<",\"snapshot_probe\":";live::snapshot_probe::emit_json(std::cout,snapshot_scope);
    std::cout<<",\"snapshot_os_probe\":";
    if(snapshot_os)snapshot_os->emit_json(std::cout);else std::cout<<"{\"enabled\":false}";
    std::cout<<",\"snapshot_clock_origin_monotonic_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(wall_start.time_since_epoch()).count()
        <<",\"slow_snapshots\":[";
    for(std::size_t i=0;i<slow_snapshot_count;++i) {if(i)std::cout<<',';sample_json(slow_snapshots[i]);}
    std::cout<<"]}\n";
    check(functional,"paced diagnostic failed: "+failure+"; cleanup: "+cleanup_error);
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
    if(script.paced) {
        using Steady=std::chrono::steady_clock;
        constexpr auto period=std::chrono::nanoseconds(1953125);
        script.pacing_anchor=Steady::now()+1ms;
        Steady::time_point first_begin{},last_begin{};
        const auto update_max=[](auto& field,std::uint64_t value) {
            auto prior=field.load();while(prior<value&&!field.compare_exchange_weak(prior,value)){}
        };
        while(!stop.stop_requested()) {
            const auto first=script.delivered.load();
            if(script.pacing_aborted.load()||first>=script.pcm.size()) {
                // Keep the device open. No EOF can provide physical END.
                std::this_thread::sleep_for(1ms);continue;
            }
            const auto ordinal=first/16;
            const auto deadline=script.pacing_anchor+period*static_cast<std::int64_t>(ordinal);
            std::this_thread::sleep_until(deadline);
            if(stop.stop_requested())return;
            const auto before=Steady::now();
            const auto late=before>deadline?static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(before-deadline).count()):0;
            if(late)script.pacing_late_callbacks.fetch_add(1);
            update_max(script.pacing_max_lateness_ns,late);
            // Deadlines never slide. Abort before delivery on a missed bound;
            // catch-up is thus bounded by ceil(20ms / period)+1 callbacks.
            if(late>20000000ULL) {
                script.pacing_abort_reason=1;script.pacing_valid=false;script.pacing_aborted=true;continue;
            }
            if(ordinal==0)first_begin=before;
            else {
                const auto interval=static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(before-last_begin).count());
                auto prior=script.pacing_min_interval_ns.load();
                while((!prior||interval<prior)&&!script.pacing_min_interval_ns.compare_exchange_weak(prior,interval)){}
                update_max(script.pacing_max_interval_ns,interval);
                script.pacing_interval_total_ns.fetch_add(interval);
            }
            last_begin=before;
            const auto end=std::min<std::size_t>(script.pcm.size(),first+16);
            bool accepted=false;
            script.callback_probe.begin(ordinal,first);
            try {
                const auto utc=(static_cast<long double>(script.utc_origin_samples)+first)/rate;
                script.now=static_cast<double>(utc);options.capture_timing(dated(utc));
                script.callback_probe.checkpoint(paced_callback_probe::Probe::Phase::provider_timing);
                const auto samples=std::span(script.pcm).subspan(first,end-first);
                if(options.capture_monitor)options.capture_monitor(samples,rate);
                script.callback_probe.checkpoint(paced_callback_probe::Probe::Phase::monitor);
                accepted=callback(samples);
                if(!accepted)script.pacing_abort_reason=2;
            } catch(const std::exception& e) {
                script.pacing_callback_error=std::string_view(e.what()).substr(0,256);script.pacing_abort_reason=3;
            } catch(...) {script.pacing_callback_error="unknown capture callback exception";script.pacing_abort_reason=3;}
            const auto after=Steady::now();
            // Keep the original deadline timestamp ahead of probe teardown.
            script.callback_probe.finish();
            const auto duration=static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(after-before).count());
            update_max(script.pacing_max_duration_ns,duration);
            script.pacing_duration_total_ns.fetch_add(duration);
            script.pacing_delivery_ns=static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(after-first_begin).count());
            script.pacing_callbacks.fetch_add(1);
            if(accepted)script.delivered=end;
            if(!accepted||after>deadline+20ms) {
                if(accepted)script.pacing_abort_reason=4;
                script.pacing_valid=false;script.pacing_aborted=true;
            }
        }
        return;
    }
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
        bool pacing_probe=false;
        bool mutex_probe=false;
        bool snapshot_probe=false;
        std::optional<std::filesystem::path> trace_path;
        for(int i=1;i<argc;++i) {
            const std::string_view argument=argv[i];
            if(argument=="--snapshot-probe") {
                check(!snapshot_probe,"duplicate snapshot probe option");snapshot_probe=true;
            } else if(argument=="--mutex-probe") {
                check(!mutex_probe,"duplicate mutex probe option");mutex_probe=true;
            } else if(argument=="--pacing-probe") {
                check(!pacing_probe,"duplicate pacing probe option");pacing_probe=true;
            } else if(argument.starts_with("--trace=")) {
                check(!trace_path,"duplicate Burst fixture trace option");
                trace_path=std::filesystem::path(std::string(argument.substr(8)));
                check(trace_path->is_absolute(),"Burst fixture trace path must be absolute");
            } else {
                check(selector.empty(),"multiple Burst fixture selectors");
                check(argument=="--timestamps" || argument=="--simulation" || argument=="--private" ||
                      argument=="--early" || argument=="--guard-contradiction" || argument=="--public" || argument=="--amplitude" ||
                      argument=="--paced-plain-off" || argument=="--paced-plain-on" ||
                      argument=="--paced-keyed-off" || argument=="--paced-keyed-on" || argument=="--paced-keyed-on-follow" ||
                      argument=="--paced-held-plain-off" || argument=="--paced-held-plain-on" ||
                      argument=="--paced-held-keyed-off" || argument=="--paced-held-keyed-on",
                      "unknown Burst fixture option");
                selector=argument;
            }
        }
        check(!pacing_probe||selector.starts_with("--paced-"),"pacing probe requires a paced selector");
        check(!mutex_probe||pacing_probe,"mutex probe requires the pacing probe");
        check(!snapshot_probe||pacing_probe,"snapshot probe requires the pacing probe");
        check(!snapshot_probe||!mutex_probe,"snapshot-only probe retains the ordinary mutex");
        std::optional<audio::trace::Scope> trace;
        if(trace_path) {
            if(selector.starts_with("--paced-"))trace.emplace(*trace_path,false,
                +[](const char* event) noexcept {
                    const std::string_view name=event;
                    return name=="trace_begin" || name=="receiver_selection" ||
                        name=="receiver_bank_admitted" || name=="receiver_signal_commit" || name=="symbol_burst_fit" ||
                        name=="pattern_receiver_engine" || name=="symbol_burst_fft_fit" || name=="symbol_burst_temporal_support";
                });
            else trace.emplace(*trace_path);
        }
        if(selector=="--paced-plain-off") {paced_burst_case(selector,false,false,false,false,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-plain-on") {paced_burst_case(selector,false,true,false,false,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-keyed-off") {paced_burst_case(selector,true,false,false,false,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-keyed-on") {paced_burst_case(selector,true,true,false,false,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-held-plain-off") {paced_burst_case(selector,false,false,false,true,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-held-plain-on") {paced_burst_case(selector,false,true,false,true,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-held-keyed-off") {paced_burst_case(selector,true,false,false,true,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        if(selector=="--paced-held-keyed-on") {paced_burst_case(selector,true,true,false,true,pacing_probe,mutex_probe,snapshot_probe);return 0;}
        // Observe the complete input after recording every original reception
        // violation; this selector remains failing diagnostic evidence, never a pass.
        if(selector=="--paced-keyed-on-follow") {paced_burst_case(selector,true,true,true,false,pacing_probe,mutex_probe,snapshot_probe);return 0;}
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
