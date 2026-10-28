#include "datapump/live.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/helper_frontend.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/resampler.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint32_t hardware_rate=48000;
constexpr double frequency_offset=2.17;
void check(bool value,const char* message) {if(!value)throw Error(message);}
double utc_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
Bytes payload() {
    constexpr std::uint64_t word=0xD6E4B39A1275CC07ULL;
    Bytes bits;
    for(unsigned remaining=64;remaining;--remaining)
        bits.push_back(static_cast<std::uint8_t>((word>>(remaining-1))&1));
    return bits;
}
std::string binary(const Bytes& bits) {
    std::string text;for(const auto bit:bits)text+=bit?'1':'0';return text;
}
live::Settings settings() {
    live::Settings value;value.device="normal helper loss capture";value.full_duplex=true;
    auto& c=value.transfer.modem;
    c.sample_rate=4000;c.carrier_hz=1000;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{0,0},{.1,0},1e8,1};
    std::array<std::uint8_t,32> secret{};
    for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(7+11*i);
    value.transfer.key=Crypto(secret);value.transfer.timestamp=0;value.transfer.search_seconds=6;
    value.transfer.clock_sync.reset();value.transfer.automatic_receive_profiles=false;
    value.transfer.recovery_options.enabled=false;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=64*1024*1024;value.content_limit=1024*1024;
    value.helper.power_w=1500;value.helper.data_power_w=4;value.helper.callsign.clear();
    check(!value.simulation && !value.transfer.timestamp && !value.transfer.clock_sync,
          "normal helper-loss fixture supplied a receive epoch or GPS clock policy");
    const auto domain=modem::oscillator_pattern_search(c);
    check(domain.hypotheses.size()>1 && !domain.limited,
          "normal helper-loss fixture lacks a complete declared oscillator search");
    return value;
}
bool trace_filter(const char* name) noexcept {
    const std::string_view event{name};
    return event=="trace_begin" || event=="receiver_signal_commit" || event=="receiver_acquisition_work" ||
        event=="receiver_bank_admitted" || event.starts_with("receiver_helper_");
}
std::uint64_t number(const std::string& line,std::string_view key) {
    const auto marker='"'+std::string(key)+"\":";const auto at=line.find(marker);
    check(at!=std::string::npos,"normal helper-loss trace omitted a required coordinate");
    return std::stoull(line.substr(at+marker.size()));
}
double decimal(const std::string& line,std::string_view key) {
    const auto marker='"'+std::string(key)+"\":";const auto at=line.find(marker);
    check(at!=std::string::npos,"normal helper-loss trace omitted a required phase fraction");
    return std::stod(line.substr(at+marker.size()));
}
struct Provider {
    live::Settings value;
    bool lose_helper;
    std::atomic<unsigned> captures=0;
    std::atomic<bool> ready=false,exhausted=false;
    std::atomic<std::uint64_t> logical_delivered=0,physical_delivered=0;
    // Published once by ready; source truth remains private to this fixture.
    double capture_epoch=0;
    std::uint64_t transmit_epoch=0,payload_end=0,helper_cut=0,total_logical=0;
};
Provider* provider=nullptr;

std::vector<float> physical_recording(Provider& script) {
    const auto& c=script.value.transfer.modem;
    // The listener starts independently. Only this private transmitter knows
    // its future cryptographic epoch; Session receives neither it nor a length.
    script.capture_epoch=std::ceil(utc_now())+4;
    const auto capture_epoch=static_cast<std::uint64_t>(script.capture_epoch);
    script.transmit_epoch=capture_epoch+6;
    auto options=script.value.transfer;options.timestamp=script.transmit_epoch;
    auto transmitter=transfer::binary_transmitter(payload(),options);
    const auto physical_origin=transmitter->physical_output_origin();
    const auto capture_origin=capture_epoch*c.sample_rate;
    const auto payload_origin=script.transmit_epoch*c.sample_rate;
    check(physical_origin>=capture_origin && physical_origin<=payload_origin,
          "private source origin escaped its future capture schedule");
    const auto lead=physical_origin-capture_origin;
    const auto origin_offset=-static_cast<long double>(payload_origin-physical_origin)/c.sample_rate;
    check(lead+modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c)==payload_origin-capture_origin,
          "private Robust first payload sample lost its actual cryptographic epoch");
    std::vector<std::complex<double>> analytic(transmitter->total_samples());
    check(transmitter->read_analytic(analytic)==analytic.size(),"private source PCM was truncated");
    const auto transmitted=transmitter->transmit_trace();auto dated_wire=payload();
    transfer::xor_binary_bits(dated_wire,options);
    check(transmitted.wire_plain_bits==payload() && transmitted.wire_bits==dated_wire,
          "private transmitter did not emit its actual dated encrypted wire");
    const auto symbol=modem::symbol_sample_count(c);
    const auto endpoint=transmitter->physical_valid_payload_end();
    script.payload_end=lead+endpoint;
    const auto tentative=origin_offset+static_cast<long double>(endpoint-symbol/2+3)/c.sample_rate;
    // Begin on an absolute400ms dwell boundary, at least2.4s before the entire
    // Robust waveform, in addition to its independent settling/training prefix.
    const auto grid_phase=static_cast<long double>(script.transmit_epoch%2);
    const long double helper_start=std::floor((grid_phase+origin_offset-2.4L)/.4L)*.4L-grid_phase;
    check(origin_offset-helper_start>=2.4L,"private helper omitted its full pre-waveform CW lead");
    const auto cutoff=helper::protected_endpoint(script.transmit_epoch,helper_start,tentative);
    check(cutoff>origin_offset+static_cast<long double>(endpoint-symbol)/c.sample_rate &&
          cutoff<origin_offset+static_cast<long double>(endpoint)/c.sample_rate,
          "protected helper cutoff escaped the final Robust symbol");
    script.helper_cut=(script.transmit_epoch-capture_epoch)*c.sample_rate+
        static_cast<std::uint64_t>(std::llround(cutoff*c.sample_rate));
    check(script.helper_cut%modem::pattern_chip_samples(c),
          "normal helper cutoff unexpectedly aligned with the native projection bin");
    std::vector<float> logical(lead+analytic.size()+10ULL*c.sample_rate);
    constexpr double tau=2*std::numbers::pi;
    for(std::size_t i=0;i<analytic.size();++i)
        logical[lead+i]=static_cast<float>(std::real(analytic[i]*
            std::polar(1.,.73+tau*frequency_offset*i/c.sample_rate)));
    auto placement=helper::plan(script.value.helper,c.carrier_hz,helper::occupied_bandwidth(c),
        hardware_rate,0,modem::outer_dsss_transmit_gain(c));
    placement.frequency_hz+=frequency_offset;
    helper::Generator carrier(script.value.helper,placement,script.transmit_epoch,true,script.value.transfer.key);
    std::mt19937_64 random(5181);std::normal_distribution<double> noise(0,.00001);
    audio::Resampler playback(c.sample_rate,hardware_rate);
    playback.set_output_transform([&](float sample,long double seconds,std::uint32_t rate) {
        const auto source_seconds=seconds-(script.transmit_epoch-capture_epoch);
        const auto end=script.lose_helper?cutoff:origin_offset+static_cast<long double>(endpoint)/c.sample_rate+8;
        // Only the independent helper summand changes on loss. Payload gain,
        // suppression/drain PCM and physical noise keep the same trajectory.
        return static_cast<float>(sample*placement.data_gain+
            carrier.mix(0,source_seconds,rate,end,helper_start)+noise(random));
    });
    std::vector<float> physical;physical.reserve(logical.size()*hardware_rate/c.sample_rate);
    std::array<float,4096> output{};std::size_t consumed=0;
    while(!playback.finished()) {
        const auto progress=playback.process(std::span(logical).subspan(consumed),output,true);
        consumed+=progress.consumed;
        physical.insert(physical.end(),output.begin(),output.begin()+progress.produced);
        check(progress.consumed || progress.produced || playback.finished(),"private playback resampler stalled");
    }
    check(consumed==logical.size(),"private playback resampler omitted source/drain samples");
    script.total_logical=logical.size();
    check(utc_now()<script.capture_epoch,"offline fixture missed its future capture schedule");
    return physical;
}

struct Result {std::string bits;bool phase_applied=false;};
Result run(bool lose_helper,bool require_phase=false) {
    auto value=settings();const auto& c=value.transfer.modem;
    Provider script{value,lose_helper};provider=&script;
    struct Release {~Release(){provider=nullptr;}}release;
    const auto directory=std::getenv("DATAPUMP_HELPER_NORMAL_LOSS_EVIDENCE");
    const auto path=(directory?std::filesystem::path(directory):std::filesystem::temp_directory_path())/
        (std::string("helper-normal-")+(lose_helper?"loss-":"continuous-")+std::to_string(audio::trace::identity())+
         "-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::filesystem::create_directories(path.parent_path());
    const auto expected=binary(payload());
    std::string actual;
    bool exact=false,narrowed=false,phase_active=false,phase_claimed=false,loss_observed=false,pending=false;
    std::uint64_t selected_id=0,last_count=0,last_revision=0,admissions=0;
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        live::Session session; // Ordinary wall clock and normal rolling admission.
        session.start(value);
        const auto deadline=std::chrono::steady_clock::now()+60s;
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();admissions=std::max(admissions,snapshot.receiver_admissions);
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "normal helper loss reset or dropped ordinary receive input");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,
                  "normal helper loss exceeded ordinary DSP accounting");
            phase_claimed=phase_claimed||snapshot.helper_phase_assistance_active;
            if(script.ready.load()) {
                const auto delivered=script.logical_delivered.load();
                if(delivered<script.helper_cut) {
                    narrowed=narrowed||(snapshot.helper_frequency_assistance_active && snapshot.helper_conditional_jobs &&
                        snapshot.helper_selected_search_pairs<snapshot.helper_original_search_pairs);
                    phase_active=phase_active||snapshot.helper_phase_assistance_active;
                }
                if(lose_helper && narrowed && delivered>script.helper_cut+c.sample_rate)
                    loss_observed=loss_observed||!snapshot.helper_frequency_assistance_active;
            }
            for(const auto& signal:snapshot.signals) {
                if(signal.helper_carrier || !signal.received_bits)continue;
                const auto raw=signal.binary?signal.text:signal.raw_bits;
                check(signal.received_bits<=expected.size() && raw==expected.substr(0,signal.received_bits),
                      "normal helper loss changed an accepted exact bit prefix");
                if(!selected_id)selected_id=signal.id;
                check(signal.id==selected_id && signal.received_bits>=last_count && signal.revision>=last_revision,
                      "normal helper loss withdrew progress or replaced the pending row");
                last_count=signal.received_bits;last_revision=signal.revision;
                pending=pending||!signal.complete;
                if(signal.complete) {
                    check(signal.received_bits==expected.size() && raw==expected && !signal.missing_symbols,
                          "normal helper loss truncated the final64-bit Robust payload");
                    check(script.ready.load() && snapshot.samples_received>=script.payload_end+6ULL*c.sample_rate,
                          "normal helper loss completed before six seconds of actual absent PCM");
                    exact=true;actual=raw;
                }
            }
            if(exact && script.exhausted.load())break;
            std::this_thread::sleep_for(5ms);
        }
        session.stop();
    }
    bool phase_applied=false,phase_work=false,phase_input_committed=false;
    bool legacy_phase_applied=false,legacy_phase_work=false;
    bool scored_end=false,trace_pending=false,later_admission=false;
    bool narrowed_work=false,later_epoch_work=false;
    std::uint64_t id=0,count=0,first_admission=UINT64_MAX,first_work_epoch=UINT64_MAX;
    std::uint64_t qualified_jobs=0,later_jobs=0,later_work_epoch=0;
    // Cumulative numeric owners are identified exactly as the joined Live
    // recorder specifies. A later admission alone does not prove any scoring.
    using RowIdentity=std::array<std::uint64_t,3>;
    std::map<RowIdentity,std::array<std::uint64_t,7>> row_work;
    std::map<RowIdentity,std::uint64_t> admitted_at; // revision, epoch, family
    struct PilotPhase {std::uint64_t sequence=0;};
    struct PhaseInput {std::uint64_t first=0,sequence=0;};
    std::map<std::uint64_t,PilotPhase> pilot_phases; // raw aperture endpoint minus one
    std::map<std::uint64_t,PhaseInput> phase_inputs; // certified logical input end
    std::uint64_t capture_revision=0;unsigned trace_scopes=0;
    // This fixture owns exactly one Session/capture. Enforce one receive
    // revision: old event schemas omit revision IDs on frontend certificates.
    std::ifstream input(path);std::string line;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"trace_begin\"")!=std::string::npos)++trace_scopes;
        const bool work=line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos;
        const bool admission=line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos;
        if(work || admission) {
            const auto revision=number(line,"revision");
            check(revision && (!capture_revision || capture_revision==revision),
                  "normal helper phase provenance crossed receiver revisions");
            capture_revision=revision;
        }
        if(line.find("\"event\":\"receiver_helper_pilot_phase\"")==std::string::npos)continue;
        const auto first=number(line,"first_sample"),end=number(line,"end_sample");
        const auto phase=number(line,"phase_sample"),applied=number(line,"applied_first_raw");
        const auto captured=number(line,"captured_end");
        const auto fraction=decimal(line,"phase_sample_fraction");
        check(std::isfinite(fraction) && fraction>=0 && fraction<1 && first<end && first<=phase && phase<end && end<=captured &&
              first<=UINT64_MAX-2*helper::AnalyticFrequencyTranslator::group_delay &&
              applied>=first+2*helper::AnalyticFrequencyTranslator::group_delay && applied<end,
              "normal helper pilot phase lacked observed raw support/capture dates");
        check(pilot_phases.emplace(end-1,PilotPhase{number(line,"sequence")}).second,
              "normal helper pilot phase reused a raw aperture endpoint");
    }
    check(trace_scopes==1 && capture_revision,"normal helper phase trace lacked a unique capture revision");
    input.clear();input.seekg(0);
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_corrected_interval\"")!=std::string::npos &&
           number(line,"common_phase_corrected")) {
            const auto first=number(line,"first_sample"),end=number(line,"end_sample");
            const auto evidence=pilot_phases.find(number(line,"raw_reference_sample"));
            // Lock also requires whole-score RF authority. Already-applied
            // phase remains accountable to its own observed dated certificate.
            check(first<end && number(line,"corrected_interval_valid") &&
                  evidence!=pilot_phases.end() && evidence->second.sequence<number(line,"sequence"),
                  "normal helper corrected phase input lacked its matching observed pilot certificate");
            check(phase_inputs.emplace(end,PhaseInput{first,number(line,"sequence")}).second,
                  "normal helper phase input reused a logical endpoint");
        }
        if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
            const auto sample=number(line,"sample");
            later_admission=later_admission||sample>0;
            first_admission=std::min(first_admission,sample);
            admitted_at[{number(line,"revision"),number(line,"epoch"),number(line,"family")}]=sample;
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
            const RowIdentity identity{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
            auto& previous=row_work[identity];
            const std::array current{number(line,"template_jobs"),number(line,"input_transforms"),
                number(line,"fft_jobs"),number(line,"direct_jobs"),number(line,"phase_jobs"),
                number(line,"phase_tracking_fits"),number(line,"phase_corrected_bins")};
            for(std::size_t i=0;i<current.size();++i)
                check(current[i]>=previous[i],"normal helper numeric work reset within one traced owner");
            const bool joined=number(line,"numeric_owner") && !number(line,"incomplete") &&
                line.find("\"reason\":\"feed_complete\"")!=std::string::npos;
            const auto original=number(line,"helper_window_original_pairs"),selected=number(line,"helper_window_selected_pairs");
            const bool qualified=joined && number(line,"realtime_helper") && number(line,"realtime_locked") &&
                number(line,"helper_window_supported") && number(line,"helper_window_active") &&
                selected && selected<original;
            const bool executed=current[0]>previous[0] && (current[2]>previous[2] || current[3]>previous[3]);
            // The dynamic window does not use legacy conditional/skip counters.
            // Use this SAME row's active selected domain and actual job delta.
            if(qualified && executed) {
                narrowed_work=true;const auto epoch=number(line,"epoch"),jobs=current[0]-previous[0];
                first_work_epoch=std::min(first_work_epoch,epoch);
                check(jobs<=UINT64_MAX-qualified_jobs,"normal helper work total overflowed");qualified_jobs+=jobs;
                const RowIdentity admission{identity[0],epoch,number(line,"family")};
                const auto found=admitted_at.find(admission);
                if(found!=admitted_at.end() && found->second>first_admission && epoch>first_work_epoch &&
                   number(line,"sample")>found->second) {
                    later_epoch_work=true;
                    if(!later_work_epoch || epoch<later_work_epoch)later_work_epoch=epoch;
                    check(jobs<=UINT64_MAX-later_jobs,"normal later-epoch work total overflowed");later_jobs+=jobs;
                }
            }
            legacy_phase_work=legacy_phase_work||(joined && (current[4]>previous[4] || current[5]>previous[5] || current[6]>previous[6]));
            phase_work=phase_work||legacy_phase_work;
            if(joined && number(line,"realtime_phase_corrected")) {
                const auto interval=phase_inputs.find(number(line,"sample"));
                check(number(line,"revision")==capture_revision && interval!=phase_inputs.end() &&
                      interval->second.first>=identity[2] && interval->second.sequence<number(line,"sequence"),
                      "normal helper joined phase input lost its receiver revision/logical dates");
                phase_input_committed=true;
                phase_applied=phase_applied||number(line,"sample")<script.helper_cut;
                // The upstream analytic translator corrects PCM before the
                // ordinary FFT. Legacy phase_view counters remain separate.
                phase_work=phase_work||(executed && current[2]>previous[2]);
            }
            previous=current;
        }
        if(line.find("\"event\":\"receiver_helper_cohort_phase_applied\"")!=std::string::npos) {
            check(number(line,"timing_qualified")==1 && number(line,"phase_jobs")>0,
                  "normal helper phase event did not represent qualified joined phase work");
            legacy_phase_applied=legacy_phase_applied||number(line,"sample")<script.helper_cut;
            phase_applied=phase_applied||legacy_phase_applied;
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        const auto received=number(line,"received_bits");if(!received)continue;
        const auto signal_id=number(line,"signal_id");if(!id)id=signal_id;
        check(id==signal_id && received>=count,"normal helper trace lost the pending payload identity or progress");
        count=received;
        const std::string marker="\"raw_bits_prefix\":\"";const auto at=line.find(marker);
        check(at!=std::string::npos,"normal helper trace omitted the exact accepted bit prefix");
        const auto first=at+marker.size(),last=line.find('"',first);
        check(line.substr(first,last-first)==expected.substr(0,std::min<std::uint64_t>(received,128)),
              "normal helper trace changed an accepted payload prefix");
        if(line.find("\"complete\":1")!=std::string::npos) {
            check(received==expected.size(),"normal helper trace completed a truncated payload");
            check(number(line,"feed_end_sample")>=number(line,"physical_end")+6ULL*c.sample_rate,
                  "normal helper trace completed before fully scored physical absence");
            scored_end=true;
        }else trace_pending=true;
    }
    std::cout<<"normal_helper_loss="<<lose_helper<<" exact="<<exact<<" bits="<<count<<" row="<<selected_id
             <<" pending="<<pending<<" scored_end="<<scored_end<<" narrowed="<<narrowed
             <<" phase_active="<<phase_active<<" phase_applied="<<phase_applied<<" phase_work="<<phase_work
             <<" phase_input_committed="<<phase_input_committed
             <<" phase_required="<<require_phase<<" phase_scope="<<(require_phase?"optional_phase_positive":"optional_measured_pilot_phase")
             <<" narrowed_work="<<narrowed_work<<" qualified_jobs="<<qualified_jobs
             <<" first_work_epoch="<<first_work_epoch<<" later_work_epoch="<<later_work_epoch
             <<" first_admission_sample="<<first_admission
             <<" later_epoch_work="<<later_epoch_work<<" later_jobs="<<later_jobs<<" loss_observed="<<loss_observed
             <<" capture_epoch="<<script.capture_epoch<<" private_tx_epoch="<<script.transmit_epoch
             <<" helper_cut="<<script.helper_cut<<" payload_end="<<script.payload_end
             <<" admissions="<<admissions<<" later_admission="<<later_admission<<" trace="<<path<<'\n';
    check(script.captures==1 && script.exhausted,"normal helper fixture reopened or truncated actual capture");
    check(exact && pending && trace_pending && scored_end && admissions>8 && later_admission,
          "normal Auto rolling helper loss lost payload, progress or scored physical END");
    check(narrowed,"normal helper fixture never used its measured carrier in ordinary reception");
    check(narrowed_work && later_epoch_work && qualified_jobs && later_jobs,
          "normal rolling helper lacked actual same-row narrowing or later-epoch scored work");
    // A phase claim requires certified corrected PCM committed to this ordinary
    // capture/revision. The optional positive mode also requires actual joined
    // FFT work; neither frequency, auxiliary decoding nor clock grants phase.
    check((!phase_claimed || phase_input_committed || (legacy_phase_applied && legacy_phase_work)) &&
          (!legacy_phase_applied || legacy_phase_work),
          "normal helper claimed phase assistance without qualified joined phase work");
    if(require_phase)check(phase_active && phase_applied && phase_work,
          "normal rolling helper phase/timing assistance never became active");
    if(lose_helper)check(loss_observed,"normal rolling receiver did not observe physical helper loss");
    if(!directory) {std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");}
    return {actual,phase_applied};
}
}

// Native receive-only provider: actual physical PCM is monitored before the
// real capture resampler. No Reference, decode outcome, epoch or length is
// injected into Session. Pacing keeps its default wall clock independently
// consistent with the recording's sample timestamps.
namespace datapump::audio {
std::vector<Device> devices() {return {{"normal helper loss capture","dated actual-PCM helper-loss fixture"}};}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format,Options options) {
    auto& script=*provider;++script.captures;
    check(rate==script.value.transfer.modem.sample_rate && hardware_rate>=options.minimum_hardware_rate,
          "normal helper provider changed the requested sample clocks");
    const auto physical=physical_recording(script);
    Resampler resampler(hardware_rate,rate);
    if(format)format({rate,hardware_rate,resampler.passband_hz(),resampler.workspace_bytes(),TimingQuality::bounded,1./hardware_rate});
    script.ready=true;
    const auto steady_origin=std::chrono::steady_clock::now()+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(script.capture_epoch-utc_now()));
    constexpr std::size_t block=hardware_rate/50;
    std::array<float,512> logical{};
    for(std::size_t first=0;first<physical.size() && !stop.stop_requested();) {
        const auto count=std::min(block,physical.size()-first);
        const auto deadline=steady_origin+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(double(first+count)/hardware_rate));
        while(!stop.stop_requested() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
        if(stop.stop_requested())return;
        const auto raw=std::span(physical).subspan(first,count);
        if(options.capture_monitor)options.capture_monitor(raw,hardware_rate);
        std::size_t consumed=0;
        while(consumed<raw.size()) {
            const auto progress=resampler.process(raw.subspan(consumed),logical);consumed+=progress.consumed;
            if(progress.produced) {
                const auto before=script.logical_delivered.load();
                if(options.capture_timing)options.capture_timing({script.capture_epoch+static_cast<long double>(before)/rate,
                    1./hardware_rate,1./rate,TimingQuality::bounded,0});
                if(!callback(std::span<const float>(logical).first(progress.produced)))return;
                script.logical_delivered+=progress.produced;
            }
            check(progress.consumed || progress.produced,"normal helper capture resampler stalled");
        }
        first+=count;script.physical_delivered=first;
    }
    // Leave capture open. No EOF/finish/zero extension may manufacture absent
    // symbols. Ten seconds of actual physical AWGN already followed the source.
    script.exhausted=true;
    while(!stop.stop_requested())std::this_thread::sleep_for(1ms);
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format) {capture(rate,device,callback,stop,std::move(format),{});}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode,Options) {
    throw Error("receive-only normal helper fixture unexpectedly transmitted");
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& callback,std::stop_token stop,
              StreamFormatCallback format,ChannelMode channels) {playback(rate,device,callback,stop,std::move(format),channels,{});}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("unexpected direct playback in normal helper fixture");
}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback) {
    throw Error("unexpected direct record in normal helper fixture");
}
}
int main(int argc,char** argv) {try {
    if(argc==2 && std::string_view(argv[1])=="--phase-positive")run(false,true);
    else if(argc==2 && std::string_view(argv[1])=="--continuous")run(false);
    else if(argc==2 && std::string_view(argv[1])=="--loss")run(true);
    else if(argc==1) {
        // Separate fresh private epochs legitimately produce different keyed
        // wire/PCM. Each source trace is checked against its own actual epoch.
        const auto continuous=run(false),loss=run(true);
        check(continuous.bits==loss.bits,"matched helper loss changed the complete Robust bit sequence");
    }else throw Error("unknown normal helper-loss fixture mode");
    return 0;
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
