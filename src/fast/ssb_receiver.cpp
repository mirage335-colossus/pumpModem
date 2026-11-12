#include "ssb_receiver.hpp"
#include "ssb_display_points.hpp"
#include "ssb_equalizer.hpp"
#include "ssb_local_fir.hpp"
#include "ssb_quiet.hpp"
#include "ssb_quiet_low_power.hpp"
#include "datapump/execution.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace datapump::fast::detail {
namespace {
constexpr std::size_t frontend_chunk=256;

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
    }

    void measure_workspace(std::size_t temporary=0) {
        const auto retained=sizeof(*this)+geometry.taps.capacity()*sizeof(double)+
            search.workspace_bytes()-sizeof(search)+frontend.workspace_bytes()-sizeof(frontend)+
            groups.capacity()*sizeof(SsbCenterRange)+fitter.workspace_bytes()-sizeof(fitter)+
            (equalizer?std::visit([](const auto& model){return model.workspace_bytes();},*equalizer):0)+
            (local_candidate?local_candidate->workspace_bytes():0)+(display_points?sizeof(SsbDisplayPoints):0);
        peak_workspace=std::max(peak_workspace,retained+temporary);
    }

    bool acquire(SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token token) {
        SsbAcquisitionDiagnostics diagnostics;
        const auto started=std::chrono::steady_clock::now();
        const auto lock=fitter.fit(view,request,token,&diagnostics);
        acquisition_diagnostics=diagnostics;
        if(acquisition_attempt_count<acquisition_attempts.size()) {
            const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            acquisition_attempts[acquisition_attempt_count++]={request.training_first_center_tick,
                view.first_center_input_tick+view.stride*view.samples.size(),request.earliest_epoch_from_training_ticks,
                request.latest_epoch_from_training_ticks,wall,diagnostics.training_nmse,diagnostics.status,
                diagnostics.marker_evaluations,diagnostics.marker_primary_rejections,diagnostics.marker_decoy_banks,
                diagnostics.marker_primary_seconds,diagnostics.marker_decoy_seconds,diagnostics.marker_row_seconds,
                diagnostics.marker_gram_seconds,diagnostics.marker_factor_seconds};
        }
        measure_workspace(diagnostics.workspace_bound_bytes);
        if(!lock || token.stop_requested())return false;
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
        if(!was_acquired) {
            const auto previous=search.workspace_bytes();
            const auto selected_centers=std::visit([](const auto& model){return model.maximum_observation_centers();},
                *equalizer);
            const auto candidate_centers=local_candidate?local_candidate->maximum_observation_centers():0;
            search.reserve_history(std::max(selected_centers,candidate_centers)+frontend_chunk);
            if(search.workspace_bytes()>previous)measure_workspace(previous);
        }
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
void SsbReceiver::finish() noexcept { impl_->frontend.finish(); }
void SsbReceiver::set_cycle_observer(CycleObserver observer) { impl_->cycle_observer=std::move(observer); }
const ModemProgress& SsbReceiver::progress() const noexcept { return impl_->state; }
const SsbCandidateSearchProgress& SsbReceiver::acquisition_progress() const noexcept { return impl_->search.progress(); }
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

SsbReceiver make_ssb_model_receiver(const Profile& profile,ValidatedIntervalSink sink,std::stop_token stop,
                                    SymbolObserver symbol_observer,SymbolObserver input_observer) {
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    return SsbReceiver(profile,make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz),
        bounds,.10,std::move(sink),stop,SsbReceiverModelPolicy::validate_local_fir,
        std::move(symbol_observer),std::move(input_observer));
}

} // namespace datapump::fast::detail
