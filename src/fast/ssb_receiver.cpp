#include "ssb_receiver.hpp"
#include "ssb_acquisition_workers.hpp"
#include "ssb_display_points.hpp"
#include "ssb_equalizer.hpp"
#include "ssb_local_fir.hpp"
#include "ssb_quiet.hpp"
#include "ssb_quiet_low_power.hpp"
#include "datapump/execution.hpp"
#include "datapump/execution_resources.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>

namespace datapump::fast::detail {
namespace {
constexpr std::size_t frontend_chunk=256;
constexpr double narrow_acoustic_offset_hz=100.;
constexpr double baseline_delay_ms=22.;
constexpr double baseline_clock_ppm=300.;
constexpr auto mib=std::size_t{1024}*1024;

double acquisition_offset_hz(const Profile& profile) noexcept {
    return supports_narrow_acoustic_fit(profile) ? narrow_acoustic_offset_hz :
        SsbAcquisitionRequest{}.max_abs_doppler_hz;
}

SsbFrontendGeometry receiver_geometry(const Profile& profile,double offset_hz) {
    if(supports_modeled_ssb_receiver(profile))
        return make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    // Bound the inverse by keeping the actual frontend grid close to the
    // periodic proposal grid. Preserve the translated audio band.
    const auto decimation=static_cast<std::size_t>(profile.sample_rate/(5.5*profile.symbol_rate));
    const auto cutoff=occupied_bandwidth_hz(profile)*.5+offset_hz;
    return make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz,cutoff,decimation);
}

std::uint64_t quiet_guard_ticks(const Profile& profile,const SsbFrontendGeometry& geometry,
                                const SsbAcquisitionRequest& limits) {
    const auto transmit_ticks=single_carrier_pulse_radius(profile)*geometry.input_rate/profile.symbol_rate;
    const auto clock_bound=std::max(limits.max_abs_clock_ratio,ssb_maximum_tracking_clock_ratio);
    const auto propagation_ticks=(transmit_ticks+limits.max_excess_delay_ticks)/(1.-clock_bound);
    const auto center_steps=std::ceil((propagation_ticks+geometry.radius())/geometry.decimation);
    if(!std::isfinite(center_steps) || center_steps<0. ||
       center_steps>=double(std::numeric_limits<std::uint64_t>::max()/geometry.decimation))
        throw std::length_error("Fast SSB quiet guard exceeds the native center grid");
    return static_cast<std::uint64_t>(center_steps)*geometry.decimation;
}

bool supported(SsbAcquisitionView view,SsbCenterRange range) {
    if(range.first<view.first_center_input_tick || range.past_last<=range.first)return false;
    const auto start=range.first-view.first_center_input_tick;
    const auto end=range.past_last-view.first_center_input_tick;
    return start%view.stride==0 && end%view.stride==0 && end/view.stride<=view.samples.size();
}

void record_score(SsbQuietProgress& progress,SsbCenterRange range,SsbPublicPresence presence,
                  const SsbQuietUnitDiagnostics& diagnostics) {
    auto* record=&progress.unscored;
    switch(presence) {
        case SsbPublicPresence::unscored: break;
        case SsbPublicPresence::present: record=&progress.present; break;
        case SsbPublicPresence::no_detection:
            record=&progress.no_detection;
            progress.no_detection_unobservable_units+=!diagnostics.observable;
            break;
        case SsbPublicPresence::absent: record=&progress.absent; break;
    }
    ++record->units;
    record->last=range;
    record->last_groups=diagnostics.count;
    record->last_observable_groups=diagnostics.informative;
    record->last_squared_coherence=diagnostics.mean_squared_coherence;
    record->last_projected_to_raw_energy=diagnostics.projected_to_raw_energy;
}

void publish_observation(const SymbolObserver& observer,std::complex<float> value) noexcept {
    try {observer(value);} catch(...) {}
}
}

struct SsbReceiver::Impl {
    using Equalizer=std::variant<SsbEqualizer,SsbLocalFir>;
    Profile profile;
    SsbFrontendGeometry geometry;
    SingleCarrierSchedule schedule;
    std::stop_token stop;
    IntervalSink sink;
    SymbolObserver symbol_observer,input_observer;
    CycleObserver cycle_observer;
    std::unique_ptr<SsbDisplayPoints> display_points;
    DelayDopplerFit fitter;
    SsbCandidateSearch search;
    std::unique_ptr<SsbFitWorkers> fit_workers;
    SsbAcquisitionWorkerProgress worker_progress;
    std::size_t fit_stack_bytes=0;
    SsbFrontend frontend;
    SsbQuietRun quiet;
    std::optional<SsbLowPowerQuiet> low_power;
    const std::uint64_t quiet_guard;
    std::optional<Equalizer> equalizer;
    std::optional<SsbLocalFir> local_candidate;
    const std::size_t intervals_per_cycle;
    const SsbReceiverModelPolicy model_policy;
    const std::size_t input_observation_stride;
    ModemProgress state;
    SsbAcquisitionDiagnostics acquisition_diagnostics;
    std::array<SsbAcquisitionAttempt,16> acquisition_attempts{};
    std::size_t acquisition_attempt_count=0;
    SsbCycleRefinementProgress refinement_progress;
    SsbQuietProgress quiet_progress;
    SsbReceiverModelProgress model_progress;
    std::uint64_t ordinal=0;
    std::size_t peak_workspace=0;
    std::vector<SsbCenterRange> groups;
    std::size_t next_group=0;
    bool payload_checked=false;
    bool observed_presence=false;

    Impl(const Profile& p,SsbFrontendGeometry g,SsbAcquisitionRequest limits,double coherence,
         IntervalSink callback,std::stop_token cancel,SsbReceiverModelPolicy policy,
         SymbolObserver observed_symbols,SymbolObserver observed_input)
        : profile(p),geometry(std::move(g)),schedule(p),stop(cancel),sink(std::move(callback)),
          symbol_observer(std::move(observed_symbols)),input_observer(std::move(observed_input)),
          fitter(profile,geometry),
          search(fitter,geometry,limits,coherence,
              [this](SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token token) {
                  return acquire(view,request,token);
              }),
          frontend(geometry,[this](const SsbFrontendOutput& block){ observe(block); },frontend_chunk),
          quiet(geometry.input_rate,geometry.decimation),quiet_guard(quiet_guard_ticks(profile,geometry,limits)),
          intervals_per_cycle(cycle_intervals(profile)),model_policy(policy),
          input_observation_stride(std::max<std::size_t>(1,std::llround(geometry.output_rate()/(2.*profile.symbol_rate)))) {
        if(!sink)throw std::invalid_argument("Fast SSB receiver requires an interval sink");
        if(symbol_observer)display_points=std::make_unique<SsbDisplayPoints>(schedule.data_symbols);
        measure_workspace();
        if(supports_modeled_ssb_receiver(profile) && model_policy==SsbReceiverModelPolicy::validate_local_fir)
            configure_workers(limits);
    }

    void configure_workers(const SsbAcquisitionRequest& limits) {
#ifndef DATAPUMP_EXECUTION_FIBERS
        execution::HostResources host{};
        try { host=execution::host_resources(); }
        catch(const Error&) {return;} // Unverified resources only admit the existing serial path.
        if(!host.hardware_threads || host.hardware_threads>std::numeric_limits<unsigned>::max()/2)
            throw std::length_error("Fast SSB worker budget overflow");
        worker_progress.hardware_threads=host.hardware_threads;
        worker_progress.worker_budget=2*host.hardware_threads;
        if(!host.available_memory_bytes)return;
        // Observed available memory already excludes the process's current
        // footprint. Reserve operating headroom and every new owner's full
        // fit, immutable observations, retained result and helper stack.
        const auto available=*host.available_memory_bytes;
        const auto headroom=std::clamp(available/10,std::uint64_t{512}*mib,std::uint64_t{2048}*mib);
        if(available<=headroom)return;
        fit_stack_bytes=execution::worker_stack_bytes();
        const auto snapshot=search.fit_view_capacity()*sizeof(std::complex<double>);
        const auto fit=fitter.fit_workspace_bound(limits)+snapshot;
        const auto owner_bytes=fit+SsbFitWorkers::result_lease_bytes()+fit_stack_bytes+2*snapshot;
        const auto owners=static_cast<unsigned>(std::min<std::uint64_t>(
            std::min(worker_progress.worker_budget,search.maximum_fit_owners()),(available-headroom)/owner_bytes));
        if(owners<2)return;
        worker_progress.owner_limit=owners;
        worker_progress.fit_lease_bytes=fit;
        fit_workers=std::make_unique<SsbFitWorkers>(owners,
            [this](SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token token) {
                return score_fit(view,request,token);
            },stop);
        search.allow_pending_fits(owners,[this](bool wait) { return commit_fits(wait); },
            [this]() -> std::optional<SsbCandidateSearch::FitFrontiers> {
                if(const auto* oldest=fit_workers ? fit_workers->oldest() : nullptr)
                    return SsbCandidateSearch::FitFrontiers{oldest->first_center,oldest->first_payload_frontier};
                return {};
            });
        note_fit_workers();
#else
        (void)limits;
#endif
    }

    void note_fit_workers() {
        if(!fit_workers)return;
        const auto pending=fit_workers->pending();
        worker_progress.pending_fits=pending;
        worker_progress.peak_pending_fits=std::max(worker_progress.peak_pending_fits,pending);
        worker_progress.started_helpers=std::max(worker_progress.started_helpers,fit_workers->started_helpers());
        const auto bytes=fit_workers->reserved_bytes(worker_progress.fit_lease_bytes,fit_stack_bytes);
        worker_progress.peak_reserved_bytes=std::max(worker_progress.peak_reserved_bytes,bytes);
        measure_workspace(bytes);
    }

    void measure_workspace(std::size_t temporary=0) {
        const auto retained=sizeof(*this)+geometry.taps.capacity()*sizeof(double)+
            search.workspace_bytes()-sizeof(search)+frontend.workspace_bytes()-sizeof(frontend)+
            groups.capacity()*sizeof(SsbCenterRange)+fitter.workspace_bytes()-sizeof(fitter)+
            (equalizer?std::visit([](const auto& model){return model.workspace_bytes();},*equalizer):0)+
            (local_candidate?local_candidate->workspace_bytes():0)+(display_points?sizeof(SsbDisplayPoints):0);
        peak_workspace=std::max(peak_workspace,retained+temporary);
    }

    SsbFitResult score_fit(SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token token) const {
        SsbFitResult result;
        const auto started=std::chrono::steady_clock::now();
        result.lock=fitter.fit(view,request,token,&result.diagnostics);
        result.wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        return result;
    }

    void discard_fits() noexcept {
        if(!fit_workers)return;
        worker_progress.discarded_fits+=fit_workers->pending();
        fit_workers->close();
        worker_progress.peak_active_fits=std::max(worker_progress.peak_active_fits,fit_workers->peak_active_fits());
        fit_workers.reset();
        worker_progress.pending_fits=0;
    }

    bool commit_fits(bool wait) {
        if(!fit_workers)return false;
        while(auto receipt=fit_workers->take(wait)) {
            note_fit_workers();
            if(receipt->failure) {
                discard_fits();
                std::rethrow_exception(receipt->failure);
            }
            if(commit_fit(receipt->request,receipt->past_last_center,receipt->result,stop))return true;
            // A full owner ring needs only its oldest result to retire.
            // Drain any other ready results, but leave later work running.
            wait=false;
        }
        return false;
    }

    std::uint64_t first_payload_frontier(SsbAcquisitionView view,const SsbAcquisitionRequest& request) const {
        const auto ticks_per_symbol=profile.sample_rate/profile.symbol_rate;
        const auto reference_offset=double((fitter.training_centers(request)/2)*view.stride);
        const auto payload_end=double(schedule.training_count+schedule.interval_symbols(0))+
            single_carrier_pulse_radius(profile);
        // Use the earliest supported epoch and fastest clock relative to the
        // fit's exact midpoint. Settle by the first complete public interval,
        // keeping all earlier pilot observations for model selection.
        const auto end_offset=reference_offset+(request.earliest_epoch_from_training_ticks-
            reference_offset+payload_end*ticks_per_symbol)/(1.+request.max_abs_clock_ratio);
        const auto past_last=view.first_center_input_tick+view.stride*view.samples.size();
        // The current fit is already ripe when its model deadline precedes
        // the offer. Use an offset from observed time to avoid epoch rounding.
        if(!std::isfinite(end_offset) || request.training_first_center_tick>past_last)
            throw std::length_error("Invalid Fast SSB payload frontier");
        if(end_offset<=0.)return past_last;
        const auto centers=static_cast<std::uint64_t>(std::floor(end_offset/view.stride));
        if(centers>(std::numeric_limits<std::uint64_t>::max()-request.training_first_center_tick)/view.stride)
            throw std::length_error("Fast SSB payload frontier exceeds its sample clock");
        return std::max(past_last,request.training_first_center_tick+centers*view.stride);
    }

    bool acquire(SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token token) {
        if(fit_workers) {
            if(fit_workers->pending()>=worker_progress.owner_limit && commit_fits(true))return true;
            if(!fit_workers || token.stop_requested())return false;
            try {
                if(fit_workers->submit(view,request,first_payload_frontier(view,request)))note_fit_workers();
                return false;
            } catch(const std::system_error&) {
                // Native task creation can fail after earlier fits started.
                // Settle their results in order before retrying this unchanged
                // snapshot on the caller, or honoring an earlier lock.
                while(fit_workers && fit_workers->pending() && !token.stop_requested())
                    if(commit_fits(true))return true;
                discard_fits();
                if(token.stop_requested())return false;
            }
        }
        const auto result=score_fit(view,request,token);
        return commit_fit(request,view.first_center_input_tick+view.stride*view.samples.size(),result,token);
    }

    bool commit_fit(const SsbAcquisitionRequest& request,std::uint64_t past_last,
                    const SsbFitResult& result,std::stop_token token) {
        if(token.stop_requested())return false;
        const auto& diagnostics=result.diagnostics;
        acquisition_diagnostics=diagnostics;
        ++worker_progress.committed_fits;
        if(acquisition_attempt_count<acquisition_attempts.size()) {
            acquisition_attempts[acquisition_attempt_count++]={request.training_first_center_tick,
                past_last,request.earliest_epoch_from_training_ticks,
                request.latest_epoch_from_training_ticks,result.wall_seconds,diagnostics.training_nmse,diagnostics.status,
                diagnostics.marker_evaluations,diagnostics.marker_primary_rejections,diagnostics.marker_decoy_banks,
                diagnostics.marker_primary_seconds,diagnostics.marker_decoy_seconds,diagnostics.marker_row_seconds,
                diagnostics.marker_gram_seconds,diagnostics.marker_factor_seconds};
        }
        if(!fit_workers)measure_workspace(diagnostics.workspace_bound_bytes);
        const auto& lock=result.lock;
        if(!lock)return false;
        discard_fits();
        if(token.stop_requested())return false;
        equalizer.emplace(std::in_place_type<SsbEqualizer>,profile,geometry,*lock,token);
        if(token.stop_requested())return false;
        if(model_policy==SsbReceiverModelPolicy::validate_local_fir &&
           SsbLocalFir::eligible(profile,geometry,*lock)) {
            model_progress.local_eligible=true;
            local_candidate.emplace(profile,geometry,*lock,token);
            if(token.stop_requested())return false;
        } else model_progress.selected=SsbReceiverModel::sparse_delay_doppler;
        state.acquired=true;
        state.clock_error_ppm=(1./(1.+lock->clock_ratio_error)-1.)*1e6;
        const auto dominant=std::ranges::max_element(lock->atoms,{},[](const auto& atom) {
            return std::norm(atom.gain);
        });
        state.carrier_error_hz=dominant->frequency_hz;
        return true;
    }

    void admit_payload_history() {
        const auto previous=search.workspace_bytes();
        const auto selected_centers=std::visit([](const auto& model){return model.maximum_observation_centers();},*equalizer);
        const auto candidate_centers=local_candidate?local_candidate->maximum_observation_centers():0;
        search.reserve_history(std::max(selected_centers,candidate_centers)+frontend_chunk);
        if(search.workspace_bytes()>previous)measure_workspace(previous);
    }

    void finish() {
        frontend.finish();
        try {
            if(fit_workers && !stop.stop_requested()) {
                while(fit_workers && fit_workers->pending() && !stop.stop_requested())search.drain_fits(stop);
                if(state.acquired && !stop.stop_requested()) {
                    admit_payload_history();
                    process_ready();
                }
            }
        } catch(...) {
            discard_fits();
            throw;
        }
        discard_fits();
    }

    void observe(const SsbFrontendOutput& block) {
        if(stop.stop_requested() || state.physical_complete)return;
        if(input_observer) {
            const auto index=(block.first_center_input_sample-geometry.radius())/block.decimation;
            const auto first=(input_observation_stride-index%input_observation_stride)%input_observation_stride;
            for(auto i=first;i<block.samples.size();i+=input_observation_stride)
                publish_observation(input_observer,static_cast<std::complex<float>>(block.samples[i]));
        }
        const auto was_acquired=state.acquired;
        search.push(block,stop);
        if(!state.acquired || stop.stop_requested())return;
        if(!was_acquired)admit_payload_history();
        process_ready();
        measure_workspace();
    }

    template<class Model>
    bool emit(Model& model,std::span<const float> soft,bool received_payload) {
        const auto position=state.intervals;
        const auto wanted=received_payload && model.wants_cycle_feedback(position);
        const auto feedback=sink(soft,wanted);
        ++state.intervals;
        if(!received_payload)++state.erased_intervals;
        if(position%intervals_per_cycle==intervals_per_cycle-1 && !stop.stop_requested()) {
            const auto cycle=position/intervals_per_cycle;
            const auto notify=[&](SsbCycleObservation stage) noexcept {
                if(cycle_observer)try {cycle_observer(cycle,stage);} catch(...) {}
            };
            notify(SsbCycleObservation::before_refinement);
            SsbCycleRefinement refinement;
            try {refinement=model.finish_coding_cycle(cycle,feedback,stop);}
            catch(...) {notify(SsbCycleObservation::refinement_failed);throw;}
            notify(SsbCycleObservation::after_refinement);
            refinement_progress.attempted+=refinement.attempted;
            refinement_progress.accepted+=refinement.accepted;
            refinement_progress.rate_updates+=refinement.accepted&&refinement.updated_rates;
            const bool phase_updated=refinement.accepted&&refinement.updated_observation_phase;
            refinement_progress.phase_updates+=phase_updated;
            if(phase_updated && !refinement_progress.first_phase_update_wire_ordinal)
                refinement_progress.first_phase_update_wire_ordinal=cycle;
            if(refinement.accepted && !refinement_progress.first_accepted_wire_ordinal)
                refinement_progress.first_accepted_wire_ordinal=cycle;
            measure_workspace(refinement.workspace_bytes);
            return phase_updated;
        }
        return false;
    }

    template<class Model>
    bool flush_holes(Model& model) {
        const std::array<float,physical_interval_bits> erasure{};
        while(state.intervals<ordinal && !stop.stop_requested()) {
            emit(model,erasure,false);
            execution::checkpoint();
        }
        return !stop.stop_requested();
    }

    void advance_interval() {
        const auto symbols=schedule.interval_symbols(static_cast<std::size_t>(ordinal));
        if(symbols>std::numeric_limits<std::uint64_t>::max()-state.symbols)
            throw std::length_error("Fast SSB symbol counter exhausted");
        state.symbols+=symbols;
        ++ordinal;
        groups.clear();
        next_group=0;
        payload_checked=false;
        observed_presence=false;
    }

    void mark_present(std::uint64_t past_last_center) {
        if(quiet_guard>std::numeric_limits<std::uint64_t>::max()-past_last_center)
            throw std::length_error("Fast SSB quiet guard exceeds the native input clock");
        // Verified symbols still have causal right support after their
        // center range. Delayed paths and the receiver FIR extend that support.
        quiet_progress.quiet_eligible_after_tick=std::max(quiet_progress.quiet_eligible_after_tick.value_or(0),
            past_last_center+quiet_guard);
        quiet.interrupt();
        quiet_progress.current_quiet_run.reset();
        if(low_power)low_power->interrupt();
        quiet_progress.low_power_ceiling.reset();
    }

    bool record_quiet(SsbAcquisitionView view,SsbCenterRange range,SsbPublicPresence presence,
                      std::optional<double> calibrated_ceiling={}) {
        if(presence==SsbPublicPresence::present)mark_present(range.past_last);
        range.first=std::max(range.first,quiet_progress.quiet_eligible_after_tick.value_or(0));
        const auto complete=range.first<range.past_last && quiet.observe(view,range,presence,calibrated_ceiling.value_or(0.));
        quiet_progress.current_quiet_run=quiet.observed_run();
        quiet_progress.longest_quiet_ticks=std::max(quiet_progress.longest_quiet_ticks,quiet.elapsed_input_ticks());
        if(calibrated_ceiling && presence==SsbPublicPresence::no_detection && quiet_progress.current_quiet_run &&
           quiet_progress.current_quiet_run->past_last==range.past_last)
            ++quiet_progress.low_power_calibrated_units;
        if(complete)state.physical_complete=true;
        return complete;
    }

    template<class Model>
    bool score_groups(Model& model,SsbAcquisitionView view) {
        if(groups.empty())groups=model.quiet_groups(ordinal);
        while(next_group<groups.size() && !stop.stop_requested()) {
            const auto range=groups[next_group];
            quiet_progress.pending_observation=range;
            if(range.first<view.first_center_input_tick)
                throw Error("Fast SSB input history lost a required public group");
            if(!supported(view,range))return false;
            SsbQuietUnitDiagnostics diagnostics;
            const auto score=model.score_group(view,ordinal,next_group,stop,{},&diagnostics);
            if(!score || stop.stop_requested())return false;
            quiet_progress.pending_observation.reset();
            record_score(quiet_progress,range,*score,diagnostics);
            observed_presence|=*score==SsbPublicPresence::present;
            ++next_group;
            if(low_power) {
                const auto decision=low_power->observe(view,range,*score,diagnostics,
                    quiet_progress.quiet_eligible_after_tick.value_or(0));
                quiet_progress.low_power_ceiling=low_power->ceiling();
                quiet_progress.low_power_coherent_veto_units+=decision.coherent_veto;
                for(const auto& receipt:std::span(decision.receipts).first(decision.count)) {
                    observed_presence|=receipt.presence==SsbPublicPresence::present;
                    if(record_quiet(view,receipt.range,receipt.presence,receipt.power_ceiling))return true;
                }
            } else if(record_quiet(view,range,*score)) {
                return true;
            }
        }
        return next_group==groups.size();
    }

    template<class Model>
    void process_ready(Model& model,SsbAcquisitionView view) {
        const std::array<float,physical_interval_bits> erasure{};
        while(!stop.stop_requested() && !state.physical_complete) {
            if(!payload_checked) {
                const auto range=model.required_centers(ordinal);
                quiet_progress.pending_observation=range;
                if(range.first<view.first_center_input_tick)
                    throw Error("Fast SSB input history lost a required native center");
                if(!supported(view,range)) {
                    if(quiet.elapsed_input_ticks() || (low_power && low_power->waiting()))score_groups(model,view);
                    return;
                }
                if(display_points)display_points->clear();
                const auto estimate=model.estimate(view,ordinal,stop,{},display_points.get());
                if(stop.stop_requested())return;
                quiet_progress.pending_observation.reset();
                if(!estimate) {
                    ++quiet_progress.unscored_payload_intervals;
                    quiet.interrupt();
                    quiet_progress.current_quiet_run.reset();
                    if(low_power)low_power->interrupt();
                    quiet_progress.low_power_ceiling.reset();
                    advance_interval();
                    continue;
                }
                measure_workspace(estimate->workspace_bytes);
                if(estimate->presence.present) {
                    quiet_progress.last_predictive_payload=range;
                    if(groups.empty())groups=model.quiet_groups(ordinal);
                    mark_present(groups.back().past_last);
                    if(!flush_holes(model))return;
                    if(!model.commit_present(*estimate,view,stop)) {
                        if(stop.stop_requested())return;
                        throw Error("Fast SSB present interval failed to commit");
                    }
                    state.last_present_signal=ReceivedSignalRange{groups.front().first,groups.back().past_last};
                    if(display_points)for(const auto point:display_points->samples())
                        publish_observation(symbol_observer,point);
                    state.evm=std::sqrt(std::max(0.,estimate->pilot_error_power));
                    if(emit(model,estimate->soft_bits,true)) {
                        const auto next_groups=model.quiet_groups(ordinal+1);
                        // Future apertures may move on the native grid. Guard
                        // both boundaries and clear any score continuity. The
                        // prior signal reference below still uses observed PCM.
                        mark_present(std::max(groups.back().past_last,next_groups.front().first));
                        measure_workspace(next_groups.capacity()*sizeof(SsbCenterRange));
                    }
                    if(low_power)(void)low_power->verified_signal(view,groups.front(),groups.back());
                    advance_interval();
                    continue;
                }
                payload_checked=true;
            }
            if(!score_groups(model,view) || state.physical_complete)return;
            if(observed_presence) {
                if(!flush_holes(model))return;
                emit(model,erasure,false);
            }
            advance_interval();
        }
    }

    void process_ready() {
        const auto view=search.retained();
        if(local_candidate && !select_model(view))return;
        std::visit([&](auto& model){process_ready(model,view);},*equalizer);
    }

    bool select_model(SsbAcquisitionView view) {
        const auto range=local_candidate->holdout_centers(0);
        if(range.first<view.first_center_input_tick)
            throw Error("Fast SSB input history lost public model selection support");
        if(!supported(view,range))return false;
        const auto receipt=local_candidate->public_holdout(view,0,stop);
        if(stop.stop_requested())return false;
        model_progress.local_tested=true;
        model_progress.selected=SsbReceiverModel::sparse_delay_doppler;
        if(receipt) {
            model_progress.local_admission_nmse=receipt->admission_nmse;
            model_progress.local_prospective_nmse=receipt->prospective_nmse;
            model_progress.local_pooled_nmse=receipt->pooled_nmse;
            model_progress.local_admission_present=receipt->admission_present;
            model_progress.local_prospective_present=receipt->prospective_present;
            measure_workspace(receipt->workspace_bytes);
            if(receipt->accepts()) {
                equalizer->emplace<SsbLocalFir>(std::move(*local_candidate));
                model_progress.selected=SsbReceiverModel::local_fir;
                low_power.emplace(geometry.input_rate,geometry.decimation,geometry.radius());
            }
        }
        local_candidate.reset();
        return true;
    }
};

SsbReceiver::SsbReceiver(const Profile& p,SsbFrontendGeometry geometry,SsbAcquisitionRequest limits,
                         double minimum_coherence,IntervalSink sink,std::stop_token stop,SsbReceiverModelPolicy policy,
                         SymbolObserver symbol_observer,SymbolObserver input_observer)
    : impl_(std::make_unique<Impl>(p,std::move(geometry),limits,minimum_coherence,std::move(sink),stop,policy,
        std::move(symbol_observer),std::move(input_observer))) {}
SsbReceiver::~SsbReceiver()=default;
SsbReceiver::SsbReceiver(SsbReceiver&&) noexcept=default;
SsbReceiver& SsbReceiver::operator=(SsbReceiver&&) noexcept=default;

std::size_t SsbReceiver::push(std::span<const float> pcm) {
    if(impl_->state.physical_complete)return 0;
    return impl_->frontend.push(pcm,impl_->stop);
}
void SsbReceiver::finish() { impl_->finish(); }
void SsbReceiver::set_input_observer(SymbolObserver observer) noexcept {
    impl_->input_observer=std::move(observer);
}
void SsbReceiver::set_cycle_observer(CycleObserver observer) { impl_->cycle_observer=std::move(observer); }
const ModemProgress& SsbReceiver::progress() const noexcept { return impl_->state; }
const SsbCandidateSearchProgress& SsbReceiver::acquisition_progress() const noexcept { return impl_->search.progress(); }
const SsbAcquisitionWorkerProgress& SsbReceiver::acquisition_worker_progress() const noexcept {
    return impl_->worker_progress;
}
const SsbAcquisitionDiagnostics& SsbReceiver::last_acquisition_diagnostics() const noexcept {
    return impl_->acquisition_diagnostics;
}
std::span<const SsbAcquisitionAttempt> SsbReceiver::first_acquisition_attempts() const noexcept {
    return std::span(impl_->acquisition_attempts).first(impl_->acquisition_attempt_count);
}
const SsbCycleRefinementProgress& SsbReceiver::refinement_progress() const noexcept {
    return impl_->refinement_progress;
}
const SsbQuietProgress& SsbReceiver::quiet_progress() const noexcept { return impl_->quiet_progress; }
const SsbReceiverModelProgress& SsbReceiver::model_progress() const noexcept { return impl_->model_progress; }
std::uint64_t SsbReceiver::consumed_input_samples() const noexcept { return impl_->frontend.input_samples(); }
std::optional<std::uint64_t> SsbReceiver::completion_tick() const noexcept { return impl_->quiet.completion_tick(); }
std::size_t SsbReceiver::workspace_bytes() const noexcept { return impl_->peak_workspace; }

bool supports_modeled_ssb_receiver(const Profile& profile) noexcept {
    if(profile.sample_rate!=48000)return false;
    if(profile.ldpc_frame_bits==16200)return supports_short_ssb_frame(profile);
    return profile.ldpc_frame_bits==64800 && profile.constellation==4096 &&
        profile.code_rate==CodeRate::eight_ninths && profile.interleave_depth==1 && standard_ssb_capacity_timing(profile);
}

bool supports_delay_doppler_receiver(const Profile& p) noexcept {
    if(dense_acoustic_framing(p))return true;
    if(supports_modeled_ssb_receiver(p))return true;
    if(p.sample_rate!=48000 || !p.capacity_mode || p.acoustic_ofdm || p.constellation>16 ||
       p.pilot_spacing_symbols>64 || p.rolloff<.10 || p.symbol_rate>2500.)return false;
    if(p.symbol_rate<800. && !supports_narrow_acoustic_fit(p))return false;
    // The frontend needs room for the translated complex baseband.
    const auto half_band=p.symbol_rate*(1+p.rolloff)*.5+acquisition_offset_hz(p);
    return half_band<std::min(p.carrier_hz,p.sample_rate*.5-p.carrier_hz);
}

SsbReceiver make_delay_doppler_receiver(const Profile& profile,ValidatedIntervalSink sink,std::stop_token stop,
                                       SymbolObserver symbol_observer,SymbolObserver input_observer,
                                       std::optional<ChannelBounds> admitted) {
    SsbAcquisitionRequest bounds{};
    const auto extent=admitted.value_or(ChannelBounds{baseline_clock_ppm,acquisition_offset_hz(profile),baseline_delay_ms});
    bounds.max_excess_delay_ticks=extent.delay_ms*.001*profile.sample_rate;
    bounds.max_abs_doppler_hz=extent.frequency_hz;
    bounds.max_abs_clock_ratio=extent.clock_ppm*1e-6;
    auto geometry=receiver_geometry(profile,bounds.max_abs_doppler_hz);
    return SsbReceiver(profile,std::move(geometry),
        bounds,.10,std::move(sink),stop,SsbReceiverModelPolicy::validate_local_fir,
        std::move(symbol_observer),std::move(input_observer));
}

} // namespace datapump::fast::detail

namespace datapump::fast {
ReceiverAssumptionBounds receiver_assumption_bounds(const Profile& p,const ChannelAssumptions& a) {
    validate(p);
    validate(a);
    const auto center=(occupied_lower_hz(p)+occupied_upper_hz(p))*.5;
    const auto clock_scale=1.-a.baseband_ppm*.5e-6;
    const auto clock_ppm=a.baseband_ppm*a.margin/clock_scale;
    const auto shift_ppm=a.shared_reference?a.baseband_ppm:a.shift_ppm;
    const auto rf_offset=a.shift_hz*shift_ppm*a.margin*1e-6/clock_scale;
    const auto doppler=a.doppler_hz*a.margin;
    const bool selective=a.selective_slow_depth>0||a.selective_fast_depth>0;
    const auto am_offset=selective?std::max(.1,doppler):0.;
    const ChannelBounds requested{clock_ppm,rf_offset+center*clock_ppm*1e-6+doppler+am_offset,a.delay_ms*a.margin};
    const auto reject=[&](std::string_view backend,std::string_view adjustment) {
        std::ostringstream message;
        message << "Fast receiver assumptions exceed " << backend << " ("
            << requested.clock_ppm << " ppm, ±" << requested.frequency_hz << " Hz, "
            << requested.delay_ms << " ms). " << adjustment;
        throw Error(message.str());
    };
    if(occupied_lower_hz(p)<=requested.frequency_hz||
       occupied_upper_hz(p)+requested.frequency_hz>=p.sample_rate*.5)
        reject("the sampled audio band","Reduce shift frequency, oscillator error or spread.");
    if(dense_acoustic_framing(p)) {
        if(clock_ppm>300.||requested.delay_ms>22.||requested.frequency_hz>160.)
            reject("the dense acoustic search","Reduce the oscillator, spread or search margin.");
        return {requested,{300.,std::max(100.,requested.frequency_hz),22.},true};
    }
    if(detail::supports_delay_doppler_receiver(p)) {
        const ChannelBounds admitted{std::max(detail::baseline_clock_ppm,clock_ppm),
            std::max(detail::acquisition_offset_hz(p),requested.frequency_hz+2.),
            std::max(detail::baseline_delay_ms,requested.delay_ms)};
        // Use the same 24 public words and grid as the real bounded fit.
        const auto fit_bins=std::floor(admitted.frequency_hz*24.*sync_symbols/p.symbol_rate);
        const auto half_band=occupied_bandwidth_hz(p)*.5+admitted.frequency_hz;
        const bool fixed_frontend=detail::supports_modeled_ssb_receiver(p);
        if(fixed_frontend&&admitted.frequency_hz>detail::acquisition_offset_hz(p))
            reject("the fixed SSB frontend","Reduce the oscillator, spread or search margin.");
        if(admitted.clock_ppm>1000.||admitted.delay_ms>24.||admitted.frequency_hz>500.||
           fit_bins>1024.||(!fixed_frontend&&half_band>=std::min(p.carrier_hz,p.sample_rate*.5-p.carrier_hz)))
            reject("the bounded delay/Doppler search","Reduce the oscillator, spread or search margin, or choose a faster supported symbol rate.");
        return {requested,admitted,true};
    }
    // Other backends retain their established fixed acquisition geometry.
    // Permit only the declared ranges their current implementations cover.
    if(p.acoustic_ofdm) {
        // Convert the tested nominal relative clocks to the same two-end
        // ratio convention used for requests.
        constexpr double clock_limit=100./(1.-100.*.5e-6);
        const auto prefix_ms=1000.*p.ofdm_prefix_samples/p.sample_rate;
        if(clock_ppm>clock_limit||rf_offset>0.||doppler>0.||selective||requested.delay_ms>=prefix_ms)
            reject("the acoustic OFDM receiver","Choose a supported single-carrier profile for oscillator shift or dynamic spread.");
        return {requested,{clock_limit,center*clock_limit*1e-6,prefix_ms},false};
    }
    constexpr double clock_limit=300./(1.-300.*.5e-6);
    const bool tracked_wire=p.channel==Channel::wire&&p.capacity_mode&&p.constellation<=256&&p.symbol_rate>=4000.;
    const auto maximum_delay=tracked_wire?.5:0.;
    const auto maximum_doppler=tracked_wire?.1:0.;
    const auto maximum_rf=tracked_wire?30.01:0.;
    if(clock_ppm>clock_limit||requested.delay_ms>maximum_delay||doppler>maximum_doppler||
       rf_offset>maximum_rf||selective)
        reject("the coherent receiver","Choose a supported delay/Doppler profile, or reduce the declared channel effects.");
    return {requested,{clock_limit,maximum_rf+center*clock_limit*1e-6+maximum_doppler,maximum_delay},false};
}

void validate_receiver_assumptions(const Profile& p,const ChannelAssumptions& a) {
    (void)receiver_assumption_bounds(p,a);
}
} // namespace datapump::fast
