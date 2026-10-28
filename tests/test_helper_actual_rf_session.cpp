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
        event=="receiver_helper_actual_rf_segment" || event=="receiver_helper_pilot_phase" ||
        event=="receiver_helper_oscillator" || event=="receiver_helper_oscillator_correction" ||
        event=="receiver_helper_corrected_interval" || event=="receiver_helper_constructor_frequency";
}
struct Laboratory {
    std::atomic<std::uint64_t> transmit_epoch=0,channels=0;
    std::optional<double> helper_end_seconds;
    std::uint64_t helper_end_sample=0;
    std::uint32_t physical_rate=0;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& event) {
        if(event.phase==live::detail::SimulationPhase::transmit_epoch)
            static_cast<Laboratory*>(opaque)->transmit_epoch=static_cast<std::uint64_t>(event.epoch);
    },1800000000.125,nullptr,[](void* opaque,modem::SampledSimulationChannel& channel) {
        auto& self=*static_cast<Laboratory*>(opaque);++self.channels;
        modem::SampledSimulationChannel::RfRetuneSimulation retunes;
        retunes.rf_shift_hz={7e6,7.7e6,8.4e6,9.1e6};
        retunes.dwell_seconds=.4;retunes.settling_seconds=.012;retunes.phase_seed=49273;
        channel.set_rf_retune_simulation(retunes);
        self.physical_rate=channel.physical_sample_rate();
        if(self.helper_end_seconds) {
            const auto sample=static_cast<long double>(*self.helper_end_seconds)*self.physical_rate;
            check(sample>=1 && sample<static_cast<long double>(UINT64_MAX),"actual RF Helper loss lies outside physical sample coordinates");
            self.helper_end_sample=static_cast<std::uint64_t>(std::round(sample));
            channel.set_lab_helper_end_sample(self.helper_end_sample);
        }
    }};
    explicit Laboratory(std::optional<double> end_seconds={}):helper_end_seconds(end_seconds) {
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
    // Evaluate the geometry from the actual double catalogue in extended
    // precision: cancellation at a cell edge can otherwise add two double
    // ULPs and fail the one-ULP comparison despite an enclosed LF endpoint.
    const auto physical_coupling=static_cast<long double>(coupling);
    const auto physical_clock_bound=static_cast<long double>(clock_bound);
    const auto physical_uncertainty=static_cast<long double>(uncertainty);
    const auto frequency_bound=physical_uncertainty+std::abs(physical_coupling)*physical_clock_bound*1e-6L;
    long double maximum_clock_radius=0;
    // Prove the entire physical RF/LF strip, including its endpoints, fits the
    // advertised timing cells. Different complete-domain constructor grids
    // need not use the original grid's exact numerical representatives.
    for(std::size_t i=0;i<ordered.size();++i) {
        const auto lower=i?(static_cast<long double>(ordered[i-1].frequency_offset_hz)+ordered[i].frequency_offset_hz)/2:-frequency_bound;
        const auto upper=i+1<ordered.size()?(static_cast<long double>(ordered[i].frequency_offset_hz)+ordered[i+1].frequency_offset_hz)/2:frequency_bound;
        const std::array endpoints{(lower-physical_uncertainty)/physical_coupling*1e6L,(lower+physical_uncertainty)/physical_coupling*1e6L,
            (upper-physical_uncertainty)/physical_coupling*1e6L,(upper+physical_uncertainty)/physical_coupling*1e6L};
        const auto lo=std::max(-physical_clock_bound,*std::min_element(endpoints.begin(),endpoints.end()));
        const auto hi=std::min(physical_clock_bound,*std::max_element(endpoints.begin(),endpoints.end()));
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
void run(const std::filesystem::path& path,bool independent_clock,bool require_oscillator_model=false,
        std::optional<double> helper_end_seconds={},bool require_prior_frequency=false) {
    const auto value=settings(independent_clock);const Bytes bits{0,0,1};
    const auto pilot=helper::plan(value.helper,helper::placement_band(value.transfer.modem),120000000,0,
        modem::outer_dsss_transmit_gain(value.transfer.modem)).frequency_hz;
    std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false,frequency=false,phase=false;
    std::uint64_t final_samples=0,peak_bytes=0;std::string status;
    Laboratory laboratory(helper_end_seconds);const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
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
    struct Segment {std::uint64_t first,settled,end,phase,rate;double rf;};
    std::map<std::uint64_t,Segment> segments;std::ifstream input(path);std::string line;
    std::uint64_t references=0,escapes=0,phase_updates=0,jobs=0,phase_work_jobs=0,stage_peak=0,pending=0,completed=0;
    std::uint64_t local_references=0,retained_clock_masks=0,narrowed_masks=0,existing_mask_opportunities=0;
    std::set<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> constructor_owners;
    ConstructorGeometry constructors;
    struct OwnerWork {std::uint64_t jobs=0,fft=0,transforms=0;};
    std::map<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>,OwnerWork> owner_jobs;
    std::uint64_t first_qualified_capture=0,first_positive_fft_frontier=0,last_work_frontier=0,worker_slots_peak=0;
    struct ConstructorInput {double uncertainty=0,center=0,expected=0;std::uint64_t pairs=0,epoch=0,family=0;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,ConstructorInput> constructor_inputs;
    std::uint64_t model_updates=0,raw_only_updates=0,phase_unqualified_updates=0,phase_fit_unqualified_updates=0;
    std::uint64_t innovation_vetoes=0,model_epoch=0,model_epoch_changes=0;
    std::uint64_t model_corrections=0,raw_escapes=0,model_escapes=0,data_diagnostic_escapes=0,correction_escapes=0;
    std::uint64_t last_model_capture=0,last_qualified_capture=0;
    std::set<double> model_rf;
    std::map<std::uint64_t,std::uint64_t> model_support_end;
    std::set<std::uint64_t> model_support_first,model_evidence_ends;
    std::uint64_t prior_corrections=0,prior_after_loss_corrections=0,postfit_corrections=0,old_schema_corrections=0;
    double prior_witness_age_max=0;
    double raw_error_max=0,model_error_max=0,data_diagnostic_error_max=0,correction_error_max=0;
    double model_uncertainty_max=0,model_uncertainty_min=INFINITY,correction_uncertainty_min=INFINITY;
    double maximum_error=0;std::uint64_t identity=0,revision=0,previous_bits=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_constructor_frequency\"")!=std::string::npos) {
            const ConstructorInput input{real(line,"helper_constructor_uncertainty_hz"),real(line,"helper_constructor_center_hz"),
                real(line,"helper_constructor_expected_hz"),number(line,"pairs"),number(line,"epoch"),number(line,"family")};
            check(std::isfinite(input.uncertainty) && input.uncertainty>0 && input.center==0 && input.expected==pilot &&
                real(line,"clock_frequency_hz")==value.transfer.modem.carrier_hz-pilot && number(line,"factory_grant"),
                "actual RF constructor trace lacks its immutable search inputs");
            const auto [found,inserted]=constructor_inputs.emplace(std::pair{number(line,"revision"),number(line,"instance_id")},input);
            check(inserted || (found->second.uncertainty==input.uncertainty && found->second.center==input.center &&
                found->second.expected==input.expected && found->second.pairs==input.pairs &&
                found->second.epoch==input.epoch && found->second.family==input.family),
                "actual RF factory retry changed its immutable search");
        }
        if(line.find("\"event\":\"receiver_helper_actual_rf_segment\"")!=std::string::npos) {
            const auto first=number(line,"first_sample"),settled=number(line,"settled_first_sample"),end=number(line,"end_sample");
            check(bool(number(line,"frequency_pooling_compatible"))==!independent_clock &&
                bool(number(line,"local_aperture_mode"))==independent_clock && !number(line,"phase_continuity_known"),
                "actual RF laboratory used an unsupported oscillator relation or invented phase continuity");
            const auto rate=number(line,"sample_rate");
            check(end-first==rate*2/5 && settled>first && settled<end,"actual RF dwell or settling date changed");
            if(!segments.empty())check(first==segments.rbegin()->second.end,"actual RF descriptor omitted ADC samples");
            segments.emplace(first,Segment{first,settled,end,number(line,"phase_segment"),rate,real(line,"actual_rf_shift_hz")});
            stage_peak=std::max(stage_peak,number(line,"raw_stage_samples"));
        }
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && number(line,"qualified")) {
            if(!first_qualified_capture)first_qualified_capture=number(line,"captured_end");
            last_qualified_capture=number(line,"captured_end");
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
        if(line.find("\"event\":\"receiver_helper_oscillator\"")!=std::string::npos) {
            const auto first=number(line,"first_sample"),end=number(line,"end_sample"),endpoint=number(line,"state_endpoint");
            auto found=segments.upper_bound(first);check(found!=segments.begin(),"oscillator fit predates actual RF mapping");--found;
            const auto& segment=found->second;const auto mean=number(line,"mean_sample");const auto fraction=real(line,"mean_fraction");
            check(first>=segment.settled && first<end && end<=segment.end-segment.rate/250 && endpoint==end-1 &&
                mean>=first && mean<end && std::isfinite(fraction) && fraction>=0 && fraction<1 &&
                number(line,"phase_segment")==segment.phase && real(line,"actual_rf_shift_hz")==segment.rf &&
                number(line,"sideband_sign")==1 && !number(line,"phase_UTC_authority") &&
                number(line,"current_hop_pilot") && number(line,"raw_frequency_valid") && number(line,"DATA_projection_diagnostic_only"),
                "oscillator observation crossed settled support or invented phase/UTC authority");
            const auto independent=real(line,"independent_variance_hz2"),residual=real(line,"residual_variance_hz2");
            const auto uncertainty=real(line,"helper_uncertainty_hz"),data_uncertainty=real(line,"DATA_uncertainty_hz");
            check(std::isfinite(independent) && independent>0 && std::isfinite(residual) && residual>=0 &&
                std::isfinite(uncertainty) && uncertainty>0 && std::isfinite(data_uncertainty) && data_uncertainty>0,
                "oscillator observation has invalid independent/residual covariance");
            const auto truth=pilot*value.simulation_clock_error_ppm*1e-6+value.simulation_frequency_offset_hz*segment.rf/7e6;
            const auto raw_error=std::abs(real(line,"raw_frequency_hz")-pilot-truth);
            const auto model_error=std::abs(real(line,"helper_frequency_hz")-pilot-truth);
            const auto data_truth=value.transfer.modem.carrier_hz*value.simulation_clock_error_ppm*1e-6+
                value.simulation_frequency_offset_hz*segment.rf/7e6;
            const auto data_error=std::abs(real(line,"DATA_residual_hz")-data_truth);
            raw_escapes+=raw_error>3*std::sqrt(independent+residual);model_escapes+=model_error>uncertainty;
            data_diagnostic_escapes+=data_error>data_uncertainty;
            raw_error_max=std::max(raw_error_max,raw_error);model_error_max=std::max(model_error_max,model_error);
            data_diagnostic_error_max=std::max(data_diagnostic_error_max,data_error);
            model_uncertainty_min=std::min(model_uncertainty_min,uncertainty);model_uncertainty_max=std::max(model_uncertainty_max,uncertainty);
            const auto epoch=number(line,"model_epoch");check(epoch>=model_epoch && epoch,"oscillator model epoch went backward");
            model_epoch_changes+=model_epoch && epoch!=model_epoch;model_epoch=epoch;
            check(!model_support_end.contains(segment.phase),"oscillator reused a full fit from the same independent hop");
            model_support_end.emplace(segment.phase,end);model_support_first.insert(first);model_evidence_ends.insert(end);model_rf.insert(segment.rf);
            raw_only_updates+=number(line,"raw_frequency_only");innovation_vetoes+=number(line,"innovation_veto");++model_updates;
            phase_unqualified_updates+=!number(line,"phase_correction_qualified");
            phase_fit_unqualified_updates+=!number(line,"phase_fit_qualified");
        }
        if(line.find("\"event\":\"receiver_helper_oscillator_correction\"")!=std::string::npos) {
            const auto first=number(line,"first_center"),last=number(line,"last_center"),captured=number(line,"captured_end");
            auto found=segments.upper_bound(first);check(found!=segments.begin(),"oscillator correction predates actual RF mapping");--found;
            const auto& segment=found->second;const auto pilot_end=number(line,"current_pilot_end");
            const auto has_field=[&](std::string_view field){return line.find('"'+std::string(field)+"\":")!=std::string::npos;};
            const auto fields=unsigned(has_field("prior_without_current_pilot"))+unsigned(has_field("frequency_query_family"))+
                unsigned(has_field("frequency_evidence_first"))+unsigned(has_field("frequency_witness_age_seconds"));
            check(fields==0 || fields==4,"oscillator correction has incomplete frequency provenance");
            // Archived pre-coast Live has none of these fields. It may prove
            // only the original post-fit path, never a fabricated prior query.
            const bool prior=fields && number(line,"prior_without_current_pilot");
            if(fields) {
                const auto evidence_first=number(line,"frequency_evidence_first"),evidence_end=number(line,"frequency_evidence_end");
                const auto family=number(line,"frequency_query_family");const auto age=real(line,"frequency_witness_age_seconds");
                check(evidence_first<evidence_end && evidence_end<=captured && family<=2 && std::isfinite(age) && age>=0 &&
                    (family==1 || age<=.5),"oscillator correction has invalid frequency witness dates or age");
                if(prior) {
                    check(first>=segment.settled && first<=last && last<segment.end && last<captured &&
                        !pilot_end && !model_support_end.contains(segment.phase) && (family==0 || family==2) &&
                        evidence_end<=segment.first && model_support_first.contains(evidence_first) && model_evidence_ends.contains(evidence_end) &&
                        std::abs(age-static_cast<double>(last-(evidence_end-1))/segment.rate)<1e-12 &&
                        number(line,"phase_segment")==segment.phase && real(line,"actual_rf_shift_hz")==segment.rf &&
                        number(line,"original_LF_UTC_union_retained") && !number(line,"phase_UTC_authority"),
                        "oscillator prior lacked an earlier independent frequency witness or invented current-hop phase");
                    ++prior_corrections;prior_witness_age_max=std::max(prior_witness_age_max,age);
                    prior_after_loss_corrections+=helper_end_seconds && segment.first>=laboratory.helper_end_sample;
                }
            } else ++old_schema_corrections;
            if(!prior) {
                check(first>=segment.settled && first<=last && last<segment.end && last<captured &&
                    number(line,"frequency_evidence_end")<=captured && pilot_end<=captured &&
                    model_support_end.contains(segment.phase) && model_support_end.at(segment.phase)==pilot_end &&
                    number(line,"phase_segment")==segment.phase && real(line,"actual_rf_shift_hz")==segment.rf &&
                    number(line,"original_LF_UTC_union_retained") && !number(line,"phase_UTC_authority"),
                    "oscillator correction lacked current dated evidence or escaped its actual RF segment");
                ++postfit_corrections;
            }
            const auto q=real(line,"applied_correction_hz"),tracking=real(line,"tracking_frequency_hz");
            const auto uncertainty=real(line,"residual_uncertainty_hz"),coefficient=real(line,"ordinary_clock_coefficient_hz");
            check(std::isfinite(uncertainty) && uncertainty>0 && std::abs(q+tracking-pilot)<1e-9 &&
                coefficient==pilot-value.transfer.modem.carrier_hz,"oscillator correction changed receiver phase/frequency coordinates");
            const auto b=value.simulation_clock_error_ppm*1e-6;
            const auto rf=value.simulation_frequency_offset_hz*segment.rf/7e6;
            const auto data_residual=rf+value.transfer.modem.carrier_hz*b+q;
            const auto error=std::abs(data_residual+coefficient*b);
            correction_escapes+=error>uncertainty;correction_error_max=std::max(correction_error_max,error);
            correction_uncertainty_min=std::min(correction_uncertainty_min,uncertainty);
            // Preserve every original B alternative. For each endpoint, the
            // conditional RF value reproduces this same observed Helper; no
            // RF/audio decomposition or UTC authority is invented by the NCO.
            const auto bound=value.transfer.modem.oscillator_search->lf.accuracy_ppm*
                value.transfer.modem.oscillator_search->margin*1e-6;
            for(const auto alternative:std::array{-bound,bound}) {
                const auto conditional_rf=rf+pilot*(b-alternative);
                const auto conditional_data=conditional_rf+value.transfer.modem.carrier_hz*alternative+q;
                const auto& policy=*value.transfer.modem.oscillator_search;
                check(std::abs(conditional_rf)<=policy.rf.accuracy_ppm*policy.margin*policy.rf_shift_hz*1e-6 &&
                    std::abs(conditional_data+coefficient*alternative)<=uncertainty+1e-12,
                    "current oscillator Helper/NCO mask lost a genuine unresolved clock endpoint");
            }
            last_model_capture=std::max(last_model_capture,captured);++model_corrections;
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
            check(current>=previous.jobs && number(line,"fft_jobs")>=previous.fft &&
                number(line,"input_transforms")>=previous.transforms,
                "actual RF ordinary work counter reset");
            if(number(line,"fft_jobs") && !first_positive_fft_frontier)first_positive_fft_frontier=number(line,"sample");
            last_work_frontier=std::max(last_work_frontier,number(line,"sample"));
            worker_slots_peak=std::max(worker_slots_peak,number(line,"coherent_fft_worker_slots_peak"));
            if(number(line,"realtime_locked") && number(line,"realtime_phase_corrected"))phase_work_jobs+=current-previous.jobs;
            if(independent_clock && number(line,"helper_window_active")) {
                check(number(line,"helper_window_clock_counts_available") && !number(line,"helper_window_clock_qualified") &&
                    number(line,"helper_window_original_clock_rates")>1 &&
                    number(line,"helper_window_original_clock_rates")==number(line,"helper_window_selected_clock_rates"),
                    "independent RF/audio local frequency discarded unresolved LF alternatives");
                ++retained_clock_masks;
                existing_mask_opportunities+=number(line,"helper_window_original_pairs")>
                    number(line,"helper_window_original_clock_rates");
                narrowed_masks+=number(line,"helper_window_selected_pairs")<number(line,"helper_window_original_pairs");
                if(constructor_owners.emplace(number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")).second) {
                    const auto birth=constructor_inputs.find({number(line,"revision"),number(line,"instance_id")});
                    check(birth!=constructor_inputs.end() && birth->second.pairs==number(line,"helper_window_original_pairs") &&
                        birth->second.epoch==number(line,"epoch") && birth->second.family==number(line,"family"),
                        "actual RF owner lacks its own immutable constructor frequency input");
                    constructors=corrected_constructor_geometry(value.transfer.modem,pilot,birth->second.uncertainty,
                        birth->second.pairs,number(line,"helper_window_original_clock_rates"),constructor_owners.size()==1);
                }
            }
            previous={current,number(line,"fft_jobs"),number(line,"input_transforms")};
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
    std::uint64_t total_jobs=0,total_fft=0,total_transforms=0;
    for(const auto& [_,row]:owner_jobs){total_jobs+=row.jobs;total_fft+=row.fft;total_transforms+=row.transforms;}
    std::cout<<"actual_rf_session,independent_clock="<<independent_clock<<",finished="<<finished<<",exact="<<exact<<",segments="<<segments.size()
        <<",references="<<references<<",frequency_escapes="<<escapes<<",max_frequency_error_hz="<<maximum_error
        <<",phase_updates="<<phase_updates<<",ordinary_jobs_peak="<<jobs<<",phase_covered_feed_jobs="<<phase_work_jobs<<",frequency_assisted="<<frequency
        <<",phase_assisted="<<phase<<",pending="<<pending<<",complete="<<completed<<",logical_samples="<<final_samples
        <<",ordinary_owner_count="<<owner_jobs.size()<<",ordinary_jobs_sum="<<total_jobs<<",ordinary_fft_jobs_sum="<<total_fft
        <<",ordinary_transforms_sum="<<total_transforms<<",worker_slots_peak="<<worker_slots_peak
        <<",first_qualified_raw_capture="<<first_qualified_capture<<",first_positive_fft_frontier="<<first_positive_fft_frontier
        <<",last_ordinary_frontier="<<last_work_frontier
        <<",oscillator_updates="<<model_updates<<",oscillator_RF_values="<<model_rf.size()<<",oscillator_raw_only_updates="<<raw_only_updates
        <<",oscillator_phase_unqualified_frequency_updates="<<phase_unqualified_updates
        <<",oscillator_phase_fit_unqualified_frequency_updates="<<phase_fit_unqualified_updates
        <<",oscillator_innovation_vetoes="<<innovation_vetoes<<",oscillator_epoch_changes="<<model_epoch_changes
        <<",oscillator_raw_frequency_escapes="<<raw_escapes<<",oscillator_raw_error_max_hz="<<raw_error_max
        <<",oscillator_helper_escapes="<<model_escapes<<",oscillator_helper_error_max_hz="<<model_error_max
        <<",oscillator_helper_U_min_hz="<<model_uncertainty_min<<",oscillator_helper_U_max_hz="<<model_uncertainty_max
        <<",oscillator_DATA_diagnostic_escapes="<<data_diagnostic_escapes<<",oscillator_DATA_diagnostic_error_max_hz="<<data_diagnostic_error_max
        <<",oscillator_corrections="<<model_corrections<<",oscillator_correction_escapes="<<correction_escapes
        <<",oscillator_prior_without_current_pilot_corrections="<<prior_corrections
        <<",oscillator_prior_after_loss_corrections="<<prior_after_loss_corrections
        <<",oscillator_prior_witness_age_max_seconds="<<prior_witness_age_max
        <<",oscillator_postfit_corrections="<<postfit_corrections<<",oscillator_old_schema_corrections="<<old_schema_corrections
        <<",oscillator_correction_error_max_hz="<<correction_error_max<<",oscillator_correction_U_min_hz="<<correction_uncertainty_min
        <<",last_oscillator_correction_capture="<<last_model_capture<<",last_qualified_capture="<<last_qualified_capture
        <<",local_reference_polls="<<local_references<<",retained_LF_masks="<<retained_clock_masks<<",narrowed_RF_masks="<<narrowed_masks
        <<",constructor_catalogues_checked="<<constructor_owners.size()<<",ordinary_constructor_pairs="<<constructors.ordinary_pairs
        <<",assisted_constructor_pairs="<<constructors.assisted_pairs<<",constructor_clock_domain_low_ppm="<<-constructors.clock_bound_ppm
        <<",constructor_clock_domain_high_ppm="<<constructors.clock_bound_ppm
        <<",existing_RF_mask_opportunities="<<existing_mask_opportunities
        <<",existing_RF_mask_narrowing_tested="<<bool(existing_mask_opportunities)
        <<",workspace_peak_bytes="<<peak_bytes<<",raw_stage_peak_samples="<<stage_peak
        <<",process_cpu_seconds="<<static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC
        <<",wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
        <<",tx_epoch_observed_only="<<laboratory.transmit_epoch<<",lab_helper_end_seconds="<<helper_end_seconds.value_or(-1)
        <<",lab_helper_end_physical_sample="<<laboratory.helper_end_sample<<",physical_sample_rate="<<laboratory.physical_rate
        <<",status="<<status<<",trace="<<path<<'\n';
    check(laboratory.channels==1 && segments.size()>=12 && references>=4 && !escapes && phase_updates>=4,
        "actual RF Session did not causally re-acquire independent retuned pilot segments");
    check(finished && exact && pending && completed && jobs && phase_work_jobs && frequency && phase,
        "actual RF Session did not complete ordinary encrypted Auto/rolling reception");
    if(independent_clock)check(local_references && retained_clock_masks && !constructor_owners.empty() &&
        constructors.assisted_pairs<constructors.ordinary_pairs && (!existing_mask_opportunities || narrowed_masks),
        "independent RF/audio local frequency did not preserve LF while narrowing available actual RF work");
    if(require_oscillator_model)check(model_updates>=4 && model_rf.size()>=2 && model_corrections &&
        !raw_escapes && !model_escapes && !data_diagnostic_escapes && !correction_escapes,
        "persistent actual RF/audio model lacked dated useful corrections or escaped its reported uncertainty");
    if(require_prior_frequency)check(prior_corrections && (!helper_end_seconds || prior_after_loss_corrections),
        "actual RF model did not use a dated prior frequency without a current-hop pilot");
}
}
int main(int argc,char** argv)try {
    bool independent_clock=false,require_oscillator_model=false,require_prior_frequency=false;
    std::optional<std::filesystem::path> requested_path;std::optional<double> helper_end_seconds;
    for(int i=1;i<argc;++i) {
        if(std::string_view(argv[i])=="--independent-clock")independent_clock=true;
        else if(std::string_view(argv[i])=="--require-oscillator-model")require_oscillator_model=true;
        else if(std::string_view(argv[i])=="--require-prior-frequency")require_prior_frequency=true;
        else if(std::string_view(argv[i])=="--helper-end-seconds") {
            check(++i<argc && !helper_end_seconds,"actual RF Helper loss requires one seconds value");
            std::size_t consumed=0;helper_end_seconds=std::stod(argv[i],&consumed);
            check(consumed==std::string_view(argv[i]).size() && std::isfinite(*helper_end_seconds) && *helper_end_seconds>0,
                "actual RF Helper loss requires finite positive seconds");
        }
        else if(!requested_path)requested_path=argv[i];
        else throw Error("unexpected actual RF fixture argument");
    }
    const auto path=requested_path.value_or(std::filesystem::temp_directory_path()/
        ("datapump-helper-actual-rf-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl"));
    check(!require_oscillator_model || independent_clock,"oscillator-model fixture requires independent RF/audio clocks");
    frozen_source_band_declines_wide_retune();mixed_output_certificates_preserve_pcm();
    run(path,independent_clock,require_oscillator_model,helper_end_seconds,require_prior_frequency);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
