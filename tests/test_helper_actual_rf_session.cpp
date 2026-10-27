#include "datapump/live.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/live_helper_frontend.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
std::uint64_t number(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("actual RF trace lacks "+std::string(key));
    return std::stoull(line.substr(at+field.size()));
}
double real(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("actual RF trace lacks "+std::string(key));
    return std::stod(line.substr(at+field.size()));
}
bool filter(const char* name)noexcept {
    const std::string_view event{name};
    return event=="trace_begin" || event=="receiver_signal_commit" ||
        event=="receiver_acquisition_work" || event=="receiver_helper_raw_reference" ||
        event=="receiver_helper_actual_rf_segment" || event=="receiver_helper_pilot_phase";
}
struct Laboratory {
    std::atomic<std::uint64_t> transmit_epoch=0,channels=0;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& event) {
        if(event.phase==live::detail::SimulationPhase::transmit_epoch)
            static_cast<Laboratory*>(opaque)->transmit_epoch=static_cast<std::uint64_t>(event.epoch);
    },1800000000.125,nullptr,[](void* opaque,modem::SampledSimulationChannel& channel) {
        auto& self=*static_cast<Laboratory*>(opaque);++self.channels;
        modem::SampledSimulationChannel::RfRetuneSimulation retunes;
        retunes.rf_shift_hz={7e6,7.7e6,8.4e6,9.1e6};
        retunes.dwell_seconds=.4;retunes.settling_seconds=.012;retunes.phase_seed=49273;
        channel.set_rf_retune_simulation(retunes);
    }};
    Laboratory() {
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"actual RF laboratory hook already installed");
    }
    ~Laboratory(){live::detail::simulation_hook.store(nullptr);}
};
live::Settings settings(bool independent_clock=false) {
    live::Settings value;value.simulation=true;value.full_duplex=true;
    auto& c=value.transfer.modem;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    // RF-only laboratory: independent audio is explicitly exact. A scalar RF
    // pooling model would be invalid for unresolved independent audio error.
    c.oscillator_search=modem::OscillatorSearchConfig{{independent_clock?100.:0.,0},{2,0},7e6,3};
    std::array<std::uint8_t,32> secret{};
    for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(7+11*i);
    value.transfer.key=Crypto(secret);value.transfer.timestamp=0;value.transfer.search_seconds=0;
    value.transfer.clock_sync.reset();value.transfer.audio_timing_error_seconds=.07;
    value.transfer.automatic_receive_profiles=false;value.transfer.recovery_options.enabled=false;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=128ULL*1024*1024;
    value.content_limit=1024*1024;value.simulation_snr_db=40;
    value.simulation_clock_error_ppm=independent_clock?100:0;value.simulation_frequency_offset_hz=7.3;
    value.simulation_phase_noise_degrees_per_sqrt_second=0;value.simulation_seed=5181;
    value.helper.power_w=1500;value.helper.data_power_w=4;value.helper.callsign.clear();value.helper.timecode=false;
    return value;
}
void frozen_source_band_declines_wide_retune() {
    const auto value=settings();const auto& config=value.transfer.modem;
    constexpr std::uint32_t rate=32000;constexpr double pilot=2450;
    helper::Reference reference;reference.sample_rate=rate;reference.frequency_valid=true;
    reference.frequency_hz=pilot+7.3;reference.frequency_uncertainty_hz=.01;
    using Frontend=live::detail::HelperInputFrontend;
    Frontend frontend(Frontend::reservation_bound(rate,config.sample_rate,pilot),93);
    frontend.admit_realtime(config,pilot,reference,rate,rate/2.-10,0);
    check(frontend.active() && !frontend.reference_passthrough(),"actual RF band fixture did not admit analytic correction");
    reference.frequency_hz=pilot+9.49;
    check(frontend.track_frequency(reference,pilot),"small actual RF shift was rejected inside its full source certificate");
    const auto correction=frontend.current_correction_hz();
    // This NCO translation remains clear of DC/Nyquist, but its actual pilot
    // modulation extends beyond the FIRST immutable source-band certificate.
    reference.frequency_hz=pilot+150;
    check(!frontend.track_frequency(reference,pilot) && frontend.current_correction_hz()==correction &&
        frontend.tracking_decline_reason(),"wide RF retune changed NCO or received a false source-band certificate");
    std::cout<<"actual_rf_source_band,small_shift_accepted=1,wide_shift_declined=1,old_nco_preserved=1\n";
}
void mixed_output_certificates_preserve_pcm() {
    const auto value=settings();const auto& config=value.transfer.modem;
    constexpr std::uint32_t rate=8000,step=160;constexpr double pilot=2450;
    helper::Reference reference;reference.sample_rate=rate;reference.frequency_valid=true;
    reference.frequency_hz=pilot+7.3;reference.frequency_uncertainty_hz=.01;
    using Frontend=live::detail::HelperInputFrontend;
    const auto grant=Frontend::reservation_bound(rate,rate,pilot);
    const auto collect=[&](int pattern) {
        Frontend frontend(grant,94);frontend.admit_realtime(config,pilot,reference,rate,rate/2.-10,0);
        std::array<float,step> input{};
        for(std::uint64_t first=0;first<2*rate;first+=step) {
            for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.02*std::cos(
                2*std::numbers::pi*(config.carrier_hz+7.3)*(first+i)/rate));
            const bool qualified=pattern==0 || (pattern==1?(first>=3200 && first<9600):first%3200<1600);
            frontend.push(input,first,qualified?std::optional<double>{.02}:std::nullopt);
            check(frontend.bytes()<=grant,"certificate split exceeded its prepaid frontend storage");
        }
        return frontend.take(UINT64_MAX,reference,2*rate);
    };
    const auto baseline=collect(0),mixed=collect(1),fragmented=collect(2);
    const auto flatten=[](const auto& blocks) {
        std::vector<float> samples;std::uint64_t next=0;
        for(const auto& block:blocks) {
            if(samples.empty())next=block.first_sample;
            check(block.first_sample==next,"certificate split dropped or redated ordinary PCM");
            samples.insert(samples.end(),block.samples.begin(),block.samples.end());next+=block.samples.size();
        }
        return samples;
    };
    const auto expected=flatten(baseline);
    check(!expected.empty() && flatten(mixed)==expected && flatten(fragmented)==expected,
        "certificate classification changed actual payload samples");
    const auto same_dates=[&](const auto& blocks) {
        return blocks.front().first_sample==baseline.front().first_sample &&
            blocks.back().first_sample+blocks.back().samples.size()==
                baseline.back().first_sample+baseline.back().samples.size();
    };
    check(same_dates(mixed) && same_dates(fragmented),
        "certificate classification changed payload support dates");
    std::size_t qualified=0,unsupported=0;
    for(const auto& block:mixed) {
        qualified+=block.corrected_rf_uncertainty_hz.has_value();unsupported+=!block.corrected_rf_uncertainty_hz;
        check(block.reference && block.reference->frequency_hz==reference.frequency_hz && block.raw_captured_end==2*rate,
            "certificate split rewrote original measured reference or captured frontier");
    }
    check(qualified && unsupported && mixed.size()<=8 && fragmented.size()<=8 &&
        !fragmented.back().corrected_rf_uncertainty_hz,
        "mixed certificate splitting lost useful support or qualified its capped unsupported tail");
    // After startup is drained, an uninterrupted certified interval retains
    // one coalesced block rather than multiplying ordinary feed calls.
    Frontend continuous(grant,95);continuous.admit_realtime(config,pilot,reference,rate,rate/2.-10,0);
    std::array<float,step> silence{};
    for(std::uint64_t first=0;first<2*rate;first+=step) {
        continuous.push(silence,first,.02);
        if(first+step==rate)(void)continuous.take(UINT64_MAX,reference,first+step);
    }
    const auto joined=continuous.take(UINT64_MAX,reference,2*rate);
    check(joined.size()==1 && joined.front().corrected_rf_uncertainty_hz,
        "homogeneous continuous input lost its large coalesced block");
    std::cout<<"actual_rf_output_certificates,mixed_blocks="<<mixed.size()<<",qualified_blocks="<<qualified
        <<",unsupported_blocks="<<unsupported<<",fragmented_capped_blocks="<<fragmented.size()
        <<",continuous_blocks="<<joined.size()<<",identical_pcm_samples="<<expected.size()<<'\n';
}
struct ConstructorGeometry {
    std::size_t ordinary_pairs=0,assisted_pairs=0,assisted_clock_rates=0;
    double clock_bound_ppm=0;
};
ConstructorGeometry corrected_constructor_geometry(const modem::Config& config,double pilot,double uncertainty,
        std::size_t actual_pairs,std::size_t actual_clock_rates,bool check_existing_component) {
    const auto ordinary=modem::oscillator_pattern_search(config,0);
    const auto corrected=modem::helper_reference_search(config,uncertainty,pilot,0,0,
        modem::HelperReferenceClockRelation::common_corrected_source,false,
        modem::PatternUtcClockGrid::complete_expanded_domain);
    const auto& catalogue=corrected.catalogue;
    const auto coupling=config.carrier_hz-pilot;
    const auto clock_bound=config.oscillator_search->lf.accuracy_ppm*config.oscillator_search->margin;
    std::set<double> clocks;auto ordered=catalogue.hypotheses;
    for(const auto& pair:ordered)clocks.insert(pair.clock_error_ppm);
    check(!catalogue.limited && ordered.size()==actual_pairs && clocks.size()==actual_clock_rates &&
        ordered.size()<ordinary.hypotheses.size() && catalogue.requested_clock_half_width_ppm==clock_bound &&
        *clocks.begin()==-clock_bound && *clocks.rbegin()==clock_bound,
        "actual corrected constructor did not reduce RF work over the full original LF domain");
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
        return a.frequency_offset_hz<b.frequency_offset_hz;
    });
    const auto frequency_bound=uncertainty+std::abs(coupling)*clock_bound*1e-6;
    double maximum_clock_radius=0;
    // Prove the entire physical RF/LF strip, including its endpoints, fits the
    // advertised timing cells. Different complete-domain constructor grids
    // need not use the original grid's exact numerical representatives.
    for(std::size_t i=0;i<ordered.size();++i) {
        const auto lower=i?(ordered[i-1].frequency_offset_hz+ordered[i].frequency_offset_hz)/2:-frequency_bound;
        const auto upper=i+1<ordered.size()?(ordered[i].frequency_offset_hz+ordered[i+1].frequency_offset_hz)/2:frequency_bound;
        const std::array endpoints{(lower-uncertainty)/coupling*1e6,(lower+uncertainty)/coupling*1e6,
            (upper-uncertainty)/coupling*1e6,(upper+uncertainty)/coupling*1e6};
        const auto lo=std::max(-clock_bound,*std::min_element(endpoints.begin(),endpoints.end()));
        const auto hi=std::min(clock_bound,*std::max_element(endpoints.begin(),endpoints.end()));
        if(lo<=hi)maximum_clock_radius=std::max({maximum_clock_radius,
            std::abs(lo-ordered[i].clock_error_ppm),std::abs(hi-ordered[i].clock_error_ppm)});
    }
    check(maximum_clock_radius<=std::nextafter(catalogue.clock_step_ppm/2,INFINITY),
        "corrected constructor lost physical LF alternatives inside a frequency cell");
    if(check_existing_component) {
        // A separate existing-catalogue control uses the SAME observed U and
        // unchanged oscillator priors in corrected input coordinates. It does
        // not claim that this Session was born with these broader RF cells.
        modem::PatternSearchResolution resolution;resolution.clock_frequency_hz=coupling;
        const auto original=modem::oscillator_pattern_search(config,0,resolution,
            modem::PatternUtcClockGrid::complete_expanded_domain);
        modem::PatternSearch search;search.hypotheses=original.hypotheses;
        search.admitted_clock_half_width_ppm=original.requested_clock_half_width_ppm;
        search.admitted_clock_cell_half_width_ppm=original.clock_step_ppm/2;
        modem::detail::PatternHelperFrequencySelector selector;
        modem::PatternHelperFrequencyWindow window{0,original.frequency.requested_half_width_hz,-coupling,
            original.clock_step_ppm/2,{}};
        check(selector.initialize(search,window) && selector.selected==search.hypotheses.size(),
            "existing corrected-coordinate catalogue did not begin with its original RF cells");
        window.half_width_hz=uncertainty;selector.update(window);
        check(selector.selected && selector.selected<search.hypotheses.size() &&
            selector.original_clock_rates>1 && selector.original_clock_rates==selector.selected_clock_rates,
            "observed local precision failed to narrow existing RF cells while retaining all original LF rates");
        std::cout<<"actual_rf_existing_selector_component,observed_U_hz="<<uncertainty
            <<",original_pairs="<<search.hypotheses.size()<<",selected_pairs="<<selector.selected
            <<",retained_original_clock_rates="<<selector.selected_clock_rates<<'\n';
        // Existing payload/END refresh coverage remains in helper_frequency_window;
        // helper_compact_acquisition covers the ordinary128-second API as well.
    }
    return {ordinary.hypotheses.size(),catalogue.hypotheses.size(),clocks.size(),clock_bound};
}
void run(const std::filesystem::path& path,bool independent_clock) {
    const auto value=settings(independent_clock);const Bytes bits{0,0,1};
    const auto pilot=helper::plan(value.helper,helper::placement_band(value.transfer.modem),120000000,0,
        modem::outer_dsss_transmit_gain(value.transfer.modem)).frequency_hz;
    std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false,frequency=false,phase=false;
    std::uint64_t final_samples=0,peak_bytes=0;std::string status;
    Laboratory laboratory;const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
    {
        audio::trace::Scope trace(path,false,&filter);
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+
            std::chrono::milliseconds(replay_ms.load());});
        session.start(value);session.transmit_bits(bits);
        const auto deadline=start+std::chrono::seconds(60);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();final_samples=snapshot.samples_received;status=snapshot.helper_status;
            peak_bytes=std::max<std::uint64_t>(peak_bytes,snapshot.dsp_buffered_bytes);
            if(!snapshot.error.empty())throw Error("actual RF Session: "+snapshot.error);
            check(!snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                "actual RF Session lost ordinary capture health");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"actual RF Session exceeded its workspace");
            frequency|=snapshot.helper_frequency_assistance_active;phase|=snapshot.helper_phase_assistance_active;
            if(snapshot.transmission_finished && snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                for(const auto& signal:session.snapshot().signals)if(!signal.helper_carrier && signal.complete)
                    exact|=signal.received_bits==bits.size() && (signal.binary?signal.text:signal.raw_bits)=="001";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        session.stop();
    }
    struct Segment {std::uint64_t first,settled,end;double rf;};
    std::map<std::uint64_t,Segment> segments;std::ifstream input(path);std::string line;
    std::uint64_t references=0,escapes=0,phase_updates=0,jobs=0,phase_work_jobs=0,stage_peak=0,pending=0,completed=0;
    std::uint64_t local_references=0,retained_clock_masks=0,narrowed_masks=0,existing_mask_opportunities=0;
    std::set<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> constructor_owners;
    ConstructorGeometry constructors;
    std::map<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>,std::uint64_t> owner_jobs;
    double maximum_error=0;std::uint64_t identity=0,revision=0,previous_bits=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_actual_rf_segment\"")!=std::string::npos) {
            const auto first=number(line,"first_sample"),settled=number(line,"settled_first_sample"),end=number(line,"end_sample");
            check(bool(number(line,"frequency_pooling_compatible"))==!independent_clock &&
                bool(number(line,"local_aperture_mode"))==independent_clock && !number(line,"phase_continuity_known"),
                "actual RF laboratory used an unsupported oscillator relation or invented phase continuity");
            const auto rate=number(line,"sample_rate");
            check(end-first==rate*2/5 && settled>first && settled<end,"actual RF dwell or settling date changed");
            if(!segments.empty())check(first==segments.rbegin()->second.end,"actual RF descriptor omitted ADC samples");
            segments.emplace(first,Segment{first,settled,end,real(line,"actual_rf_shift_hz")});
            stage_peak=std::max(stage_peak,number(line,"raw_stage_samples"));
        }
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && number(line,"qualified")) {
            const auto date=number(line,"sample");auto found=segments.upper_bound(date);
            check(found!=segments.begin(),"actual RF fit predates its observed mapping");--found;
            check(date>=found->second.settled && date<found->second.end,"actual RF fit used retune settling or another phase segment");
            if(independent_clock) {
                check(number(line,"frequency_local_aperture") && number(line,"frequency_first_sample")>=found->second.settled &&
                    number(line,"frequency_context_first_sample")==number(line,"frequency_first_sample") &&
                    number(line,"frequency_context_end_sample")==date+1,
                    "independent RF/audio fit escaped its own local aperture");
                ++local_references;
            }
            const auto truth=pilot*(1+value.simulation_clock_error_ppm*1e-6)+
                value.simulation_frequency_offset_hz*found->second.rf/7e6;
            const auto error=std::abs(real(line,"frequency_hz")-truth);maximum_error=std::max(maximum_error,error);
            escapes+=error>real(line,"uncertainty_hz");++references;
        }
        if(line.find("\"event\":\"receiver_helper_pilot_phase\"")!=std::string::npos) {
            const auto first=number(line,"first_sample"),end=number(line,"end_sample");auto found=segments.upper_bound(first);
            check(found!=segments.begin(),"actual RF phase lacks an actual segment");--found;
            check(first>=found->second.settled && end<=found->second.end && end<=number(line,"captured_end"),
                "actual RF phase fit crossed an unknown retune or used future samples");++phase_updates;
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
            const auto current=number(line,"template_jobs");jobs=std::max(jobs,current);
            auto& previous=owner_jobs[{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")}];
            check(current>=previous,"actual RF ordinary work counter reset");
            if(number(line,"realtime_locked") && number(line,"realtime_phase_corrected"))phase_work_jobs+=current-previous;
            if(independent_clock && number(line,"helper_window_active")) {
                check(number(line,"helper_window_clock_counts_available") && !number(line,"helper_window_clock_qualified") &&
                    number(line,"helper_window_original_clock_rates")>1 &&
                    number(line,"helper_window_original_clock_rates")==number(line,"helper_window_selected_clock_rates"),
                    "independent RF/audio local frequency discarded unresolved LF alternatives");
                ++retained_clock_masks;
                existing_mask_opportunities+=number(line,"helper_window_original_pairs")>
                    number(line,"helper_window_original_clock_rates");
                narrowed_masks+=number(line,"helper_window_selected_pairs")<number(line,"helper_window_original_pairs");
                if(constructor_owners.emplace(number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")).second)
                    constructors=corrected_constructor_geometry(value.transfer.modem,pilot,
                        real(line,"realtime_reference_uncertainty_hz"),number(line,"helper_window_original_pairs"),
                        number(line,"helper_window_original_clock_rates"),constructor_owners.size()==1);
            }
            previous=current;
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")!=std::string::npos && number(line,"received_bits")) {
            const auto count=number(line,"received_bits"),id=number(line,"signal_id");
            if(!identity)identity=id;
            // This focused fixture requires one canonical pending row. A
            // secondary suffix is a failure, not an excuse to merge identities.
            check(id==identity && count>=previous_bits && count<=bits.size() && number(line,"revision")>=revision &&
                !number(line,"missing"),"actual RF ordinary row lost identity or exact monotonic progress");
            revision=number(line,"revision");previous_bits=count;
            const auto prefix="\"raw_bits_prefix\":\"";const auto begin=line.find(prefix);
            check(begin!=std::string::npos,"actual RF ordinary progress lacks raw bits");
            const auto offset=begin+std::char_traits<char>::length(prefix),end=line.find('"',offset);
            check(line.substr(offset,end-offset)==std::string("001").substr(0,count),"actual RF ordinary prefix changed");
            if(number(line,"complete")) {
                check(count==bits.size(),"actual RF ordinary END completed a suffix");++completed;
                const auto symbol=number(line,"symbol_samples"),feed=number(line,"feed_end_sample"),end_sample=number(line,"physical_end");
                const auto sample_rate=value.transfer.modem.sample_rate;
                check(symbol && feed>=end_sample && feed-end_sample>=((6ULL*sample_rate+symbol-1)/symbol)*symbol,
                    "actual RF END lacked consecutive fully scored six-second absence");
            } else ++pending;
        }
    }
    std::cout<<"actual_rf_session,independent_clock="<<independent_clock<<",finished="<<finished<<",exact="<<exact<<",segments="<<segments.size()
        <<",references="<<references<<",frequency_escapes="<<escapes<<",max_frequency_error_hz="<<maximum_error
        <<",phase_updates="<<phase_updates<<",ordinary_jobs_peak="<<jobs<<",phase_covered_feed_jobs="<<phase_work_jobs<<",frequency_assisted="<<frequency
        <<",phase_assisted="<<phase<<",pending="<<pending<<",complete="<<completed<<",logical_samples="<<final_samples
        <<",local_reference_polls="<<local_references<<",retained_LF_masks="<<retained_clock_masks<<",narrowed_RF_masks="<<narrowed_masks
        <<",constructor_catalogues_checked="<<constructor_owners.size()<<",ordinary_constructor_pairs="<<constructors.ordinary_pairs
        <<",assisted_constructor_pairs="<<constructors.assisted_pairs<<",constructor_clock_domain_low_ppm="<<-constructors.clock_bound_ppm
        <<",constructor_clock_domain_high_ppm="<<constructors.clock_bound_ppm
        <<",existing_RF_mask_opportunities="<<existing_mask_opportunities
        <<",existing_RF_mask_narrowing_tested="<<bool(existing_mask_opportunities)
        <<",workspace_peak_bytes="<<peak_bytes<<",raw_stage_peak_samples="<<stage_peak
        <<",process_cpu_seconds="<<static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC
        <<",wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
        <<",tx_epoch_observed_only="<<laboratory.transmit_epoch<<",status="<<status<<",trace="<<path<<'\n';
    check(laboratory.channels==1 && segments.size()>=12 && references>=4 && !escapes && phase_updates>=4,
        "actual RF Session did not causally re-acquire independent retuned pilot segments");
    check(finished && exact && pending && completed && jobs && phase_work_jobs && frequency && phase,
        "actual RF Session did not complete ordinary encrypted Auto/rolling reception");
    if(independent_clock)check(local_references && retained_clock_masks && !constructor_owners.empty() &&
        constructors.assisted_pairs<constructors.ordinary_pairs && (!existing_mask_opportunities || narrowed_masks),
        "independent RF/audio local frequency did not preserve LF while narrowing available actual RF work");
}
}
int main(int argc,char** argv)try {
    bool independent_clock=false;std::optional<std::filesystem::path> requested_path;
    for(int i=1;i<argc;++i) {
        if(std::string_view(argv[i])=="--independent-clock")independent_clock=true;
        else if(!requested_path)requested_path=argv[i];
        else throw Error("unexpected actual RF fixture argument");
    }
    const auto path=requested_path.value_or(std::filesystem::temp_directory_path()/
        ("datapump-helper-actual-rf-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl"));
    frozen_source_band_declines_wide_retune();mixed_output_certificates_preserve_pcm();run(path,independent_clock);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
