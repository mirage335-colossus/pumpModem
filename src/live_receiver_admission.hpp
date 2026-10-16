#pragma once
// Shared metadata extraction from Live admission. This does not allocate a
// receiver, grant memory, score PCM, retire a receiver or classify absence.
#include "datapump/live.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
namespace datapump::live::detail {
struct ReceiverAdmissionContext {
    std::uint64_t sample_origin=0,observed_sample=0;
    std::optional<audio::TimePrediction> timing;
    double now=0,propagation=0;
    std::size_t key_count=1,profile_count=1,capacity=0,capture_owner_bytes=0,control_margin=0;
    bool keyed=true,prefer_streamed_templates=false,compact_clock_search=false;
};
struct ReceiverSearchPreparation {
    modem::PatternSearch search;
    std::optional<modem::OscillatorPatternSearch> declared_search;
    bool clock_window_modeled=false;
};
inline bool modeled_receiver_start_window(modem::PatternSearch& search,const modem::Config& config,
            std::uint64_t epoch,const ReceiverAdmissionContext& context,const Settings& value,std::uint64_t origin) {
        const bool calendar=transfer::shared_hop_calendar(config);
        const auto policy=clock_sync::effective_policy(value.transfer.clock_sync,calendar);
        if(!policy || value.simulation || !context.timing ||
           context.timing->quality==audio::TimingQuality::unavailable ||
           (!calendar && context.timing->uncertainty_seconds>clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds)) || !search.start_offset_seconds ||
           !(calendar ||
             search.compact_clock_search ||
             (modem::symbol_sample_count(config)<16ULL*config.sample_rate && (config.scramble||config.dsss) &&
              config.spreading_mode==modem::SpreadingMode::pattern && !search.hypotheses.empty() &&
              !search.couple_clock_to_carrier)) ||
           (!calendar && (!config.oscillator_search || (config.oscillator_search->reference==modem::OscillatorReference::shared_radio &&
             config.oscillator_search->rf_shift_hz!=0))))return false;
        const auto& stamp=*context.timing;
        auto minimum_phase=config.stream_phase_samples,maximum_phase=config.stream_phase_samples;
        if(search.search_stream_phases && (config.scramble || config.dsss)) {
            const auto step=modem::canonical_phase_step(config);
            minimum_phase=0;
            maximum_phase=modem::canonical_phase_cell_upper(config,
                (modem::canonical_phase_extent(config)-1)/step*step);
        }
        if(modem::deferred_symbols(config)) {
            minimum_phase+=modem::scheduled_symbol_offset(config,epoch,minimum_phase,0);
            maximum_phase+=modem::scheduled_symbol_offset(config,epoch,maximum_phase,0);
        }
        const auto map=(calendar?clock_sync::calendar_arrival_map:clock_sync::audio_lag_arrival_map)(*policy,epoch,stamp.utc_seconds,
            static_cast<long double>(context.observed_sample)-origin,config.sample_rate,stamp.seconds_per_frame,
            stamp.rate_uncertainty_fraction,std::max(stamp.uncertainty_seconds,clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds)),
            maximum_phase,value.transfer.audio_timing_error_seconds);
        if(!map)return false;
        const auto lower=(*search.start_offset_seconds-search.start_uncertainty_seconds)*config.sample_rate;
        const auto upper=(*search.start_offset_seconds+search.start_uncertainty_seconds)*config.sample_rate;
        // Qualify the intersection with the existing bank, not containment of
        // the entire prior. Edge cells may extend past this bank's original
        // domain; the acquisition selectors retain the original bounds/cells.
        if(!calendar && (map->origin_samples+map->phase_scale*maximum_phase+map->half_width_samples<lower ||
           map->origin_samples+map->phase_scale*minimum_phase-map->half_width_samples>upper))return false;
        // For a dated hop calendar an empty intersection is an empty epoch,
        // not permission to translate that calendar through the broad domain.
        // All original message epochs, phase cells and oscillator lanes remain
        // present; the catalog intersects their JOINT arrival relation.
        search.qualified_start_window=modem::PatternStartWindow{map->origin_samples,map->phase_scale,map->half_width_samples};
        return true;
    }
inline ReceiverSearchPreparation prepare_receiver_search(const modem::Config& config,
        const Settings& value,std::uint64_t epoch,const ReceiverAdmissionContext& context,modem::PatternSearch initial={}) {
    ReceiverSearchPreparation result{std::move(initial),{},false};
    auto& search=result.search;
    auto& declared_search=result.declared_search;
    search.receiver_schedule_key_count=context.key_count;
    search.receiver_bank_count=modem::receiver_strategy_bank_count(config,value.transfer.search_seconds,context.key_count,context.profile_count);
    const auto owner_charge=context.capture_owner_bytes;
    const auto cohort_share=(context.capacity-std::min(context.capacity,owner_charge))/std::max<std::size_t>(1,search.receiver_bank_count);
    search.receiver_strategy_workspace_bytes=cohort_share>context.control_margin?cohort_share-context.control_margin:1;
    search.input_sample_origin=context.sample_origin;
    if(config.oscillator_search) {
        auto oscillator_search=modem::oscillator_pattern_search(config,transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(config):0);
        declared_search=oscillator_search;
        search.admitted_frequency_half_width_hz=oscillator_search.frequency.requested_half_width_hz;
        search.admitted_clock_half_width_ppm=oscillator_search.requested_clock_half_width_ppm;
        search.complex_frequency_coverage=!oscillator_search.frequency.limited;
        search.complex_clock_coverage=oscillator_search.clock_half_width_ppm>=oscillator_search.requested_clock_half_width_ppm;
        search.hypotheses=std::move(oscillator_search.hypotheses);
    }
    else search.expand_clock_search=true;
    search.prefer_streamed_templates=context.prefer_streamed_templates;
    search.allow_local_clock_fallback=true;
    search.compact_clock_search=context.compact_clock_search;
    search.search_stream_phases=context.keyed;
    search.bit_limit=transfer::pattern_bit_limit(value.content_limit);
    // A fixed diagnostic message epoch does not replace a dated
    // capture origin. Scheduled settling can span null hops;
    // adding its unscheduled source duration would move this
    // bank away from the actual physical calendar intersection.
    const bool dated_calendar=!value.simulation && transfer::shared_hop_calendar(config) && context.timing;
    search.start_offset_seconds=static_cast<double>(epoch)-
        (value.transfer.timestamp && !dated_calendar?static_cast<double>(value.transfer.timestamp):context.now);
    if(!value.simulation && (!value.transfer.timestamp || dated_calendar)) *search.start_offset_seconds+=context.propagation;
    if(value.simulation || (value.transfer.timestamp && !dated_calendar))
        *search.start_offset_seconds+=(static_cast<double>(modem::training_sample_count(config))+
            static_cast<double>(modem::pattern_pulse_padding_samples(config)))/config.sample_rate;
    search.start_uncertainty_seconds=value.transfer.search_seconds+1.;
    result.clock_window_modeled=modeled_receiver_start_window(search,config,epoch,context,value,context.sample_origin);
    if(modem::scheduled_bursts(config) && !modem::packed_scheduled_symbols(config) && config.receiver_strategy!=modem::ReceiverStrategy::amplitude_only) {
        auto refined=modem::scheduled_complex_search(config,search,
            transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(config):0);
        if(refined.complete_coverage)search=std::move(refined.search);
    }
    return result;
}
} // namespace datapump::live::detail
