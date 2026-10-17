#pragma once
// Shared metadata extraction from Live admission. This does not allocate a
// receiver, grant memory, score PCM, retire a receiver or classify absence.
#include "datapump/live.hpp"
#include "live_receiver_layout.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>
#include <span>
#include <utility>
namespace datapump::live::detail {
struct ReceiverAdmissionContext {
    std::uint64_t sample_origin=0,observed_sample=0;
    std::optional<audio::TimePrediction> timing;
    std::optional<long double> public_amplitude_epoch_origin;
    double now=0,propagation=0;
    std::size_t key_count=1,profile_count=1,capacity=0,capture_owner_bytes=0,control_margin=0;
    bool keyed=true,prefer_streamed_templates=false,compact_clock_search=false;
    std::optional<std::size_t> amplitude_family_bank_count;
};
struct ReceiverSearchPreparation {
    modem::PatternSearch search;
    std::optional<modem::OscillatorPatternSearch> declared_search;
    bool clock_window_modeled=false;
};
inline void prepare_public_amplitude_windows(const modem::Config& config,const Settings& value,
        bool keyed,modem::PatternSearch& search,std::size_t families=1,std::size_t profiles=1) {
    if(keyed || families!=1 || profiles!=1 || value.transfer.key || value.simulation || value.transfer.timestamp || value.transfer.clock_sync ||
       config.scramble || config.data_key || config.dsss || config.dsss_factor!=1 ||
       config.fake_hopping || modem::scheduled_bursts(config) || config.symbol_burst.enabled())return;
    const auto chip=modem::pattern_chip_samples(config);
    const auto geometry=modem::pattern_amplitude_geometry(config,search);
    // Preserve the original whole-second/half-chip representatives exactly.
    // Other rate lattices retain their preceding admission policy.
    if(!geometry.supported || geometry.clock_hypotheses!=1 || chip%2 || !chip ||
       config.sample_rate%(chip/2))return;
    search.amplitude_epoch_stride_seconds=64;
    search.amplitude_start_uncertainty_seconds=std::max(search.start_uncertainty_seconds,33.);
}
inline bool modeled_receiver_start_window(modem::PatternSearch& search,const modem::Config& config,
            std::uint64_t epoch,const ReceiverAdmissionContext& context,const Settings& value,std::uint64_t origin,bool amplitude=false) {
        // Calendar scheduling does not upgrade Default's station-clock prior
        // to GPS accuracy. Preserve its complete several-second acquisition.
        const bool calendar=transfer::shared_hop_calendar(config);
        const auto policy=config.symbol_burst.enabled() && !value.transfer.clock_sync?
            std::optional<clock_sync::Policy>{{(value.transfer.search_seconds+1.)/2,0,0}}:
            clock_sync::effective_policy(value.transfer.clock_sync,calendar);
        if(!policy || value.simulation || !context.timing ||
           context.timing->quality==audio::TimingQuality::unavailable ||
           (!calendar && context.timing->uncertainty_seconds>clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds)) || !search.start_offset_seconds ||
           !(amplitude || calendar ||
             search.compact_clock_search ||
             (modem::symbol_sample_count(config)<16ULL*config.sample_rate && (config.scramble||config.dsss) &&
              config.spreading_mode==modem::SpreadingMode::pattern && !search.hypotheses.empty() &&
              !search.couple_clock_to_carrier)) ||
           (!amplitude && !calendar && (!config.oscillator_search || (config.oscillator_search->reference==modem::OscillatorReference::shared_radio &&
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
inline std::size_t amplitude_epoch_lookahead(const modem::Config& config,const modem::PatternSearch& search,
        long double seconds_per_frame,long double rate_uncertainty) {
    if((!search.amplitude_start_window && search.amplitude_epoch_stride_seconds==1) || !std::isfinite(seconds_per_frame) || seconds_per_frame<=0 ||
       !std::isfinite(rate_uncertainty) || rate_uncertainty<0)return 0;
    const auto geometry=modem::pattern_amplitude_geometry(config,search);
    if(!geometry.supported)return 0;
    // refresh_bank runs before each <=plot_size feed, on UTC-second changes.
    // Admit early enough that even the ORIGINAL window's lower bound is at
    // least one FFT block ahead. A dated subset cannot require earlier input.
    // This remains conservative when the affine prior grows with future time.
    const auto chunk=plot_size*std::max(1.L/config.sample_rate,seconds_per_frame+rate_uncertainty/config.sample_rate);
    const auto lead=std::ceil(search.amplitude_start_uncertainty_seconds.value_or(search.start_uncertainty_seconds)+geometry.block_seconds+chunk+1);
    return static_cast<std::size_t>(std::min(lead,static_cast<long double>(std::numeric_limits<std::size_t>::max())));
}
inline ReceiverSearchPreparation prepare_receiver_search(const modem::Config& config,
        const Settings& value,std::uint64_t epoch,const ReceiverAdmissionContext& context,modem::PatternSearch initial={}) {
    ReceiverSearchPreparation result{std::move(initial),{},false};
    auto& search=result.search;
    auto& declared_search=result.declared_search;
    const bool public_burst=config.symbol_burst.enabled() && !config.scramble && !config.dsss;
    if(public_burst)search.initial_stream_symbols=1;
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
    // A Burst support starts on the half-minute grid. Its payload phase is
    // exactly P%Fs, independently of public/private waveform selection.
    search.search_stream_phases=context.keyed && !config.symbol_burst.enabled();
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
    if(value.transfer.clock_sync&&!search.qualified_start_window) {
        auto amplitude_search=search;
        if(modeled_receiver_start_window(amplitude_search,config,epoch,context,value,context.sample_origin,true))
            search.amplitude_start_window=amplitude_search.qualified_start_window;
    }
    if(modem::scheduled_bursts(config) && !modem::packed_scheduled_symbols(config) && config.receiver_strategy!=modem::ReceiverStrategy::amplitude_only) {
        auto refined=modem::scheduled_complex_search(config,search,
            transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(config):0);
        if(refined.complete_coverage)search=std::move(refined.search);
    }
    // Establish the finite amplitude relation before computing its FFT lead.
    // The lead admits future UTC-grid cohorts before their first full window.
    if(public_burst && !search.amplitude_start_window && search.qualified_start_window)
        search.amplitude_start_window=search.qualified_start_window;
    if(!value.transfer.timestamp&&!modem::scheduled_bursts(config)) {
        prepare_public_amplitude_windows(config,value,context.keyed,search,context.key_count,context.profile_count);
        if(search.amplitude_epoch_stride_seconds>1 && context.public_amplitude_epoch_origin)
            search.amplitude_start_offset_seconds=static_cast<double>(static_cast<long double>(epoch)-
                *context.public_amplitude_epoch_origin-static_cast<long double>(context.sample_origin)/config.sample_rate);
        if(search.amplitude_epoch_stride_seconds>1)
            search.amplitude_lookahead_epochs=amplitude_epoch_lookahead(config,search,1.L/config.sample_rate,0);
        if(!context.keyed&&context.timing)
            search.amplitude_lookahead_epochs=std::max(search.amplitude_lookahead_epochs,amplitude_epoch_lookahead(config,search,
                context.timing->seconds_per_frame,context.timing->rate_uncertainty_fraction));
        search.amplitude_receiver_bank_count=modem::receiver_strategy_amplitude_bank_count(config,search,
            context.key_count,context.profile_count);
        const auto amplitude_share=(context.capacity-std::min(context.capacity,owner_charge))/
            std::max<std::size_t>(1,context.amplitude_family_bank_count.value_or(search.amplitude_receiver_bank_count));
        search.amplitude_strategy_workspace_bytes=amplitude_share>context.control_margin?
            amplitude_share-context.control_margin:1;
    }
    if(modem::symbol_burst_public_rolling_search(config) && !value.transfer.timestamp &&
       !search.compact_clock_search && config.receiver_strategy!=modem::ReceiverStrategy::amplitude_only) {
        // Preserve finite geometry for an amplitude fallback, while the
        // existing native FFT keeps its original rolling first-symbol search.
        search.amplitude_start_offset_seconds=search.start_offset_seconds;
        search.amplitude_start_uncertainty_seconds=search.start_uncertainty_seconds;
        search.amplitude_start_window=search.qualified_start_window;
        search.start_offset_seconds.reset();
        search.qualified_start_window.reset();
        result.clock_window_modeled=false;
        search.receiver_bank_count=context.key_count*context.profile_count;
        const auto native_share=(context.capacity-std::min(context.capacity,owner_charge))/
            std::max<std::size_t>(1,search.receiver_bank_count);
        search.receiver_strategy_workspace_bytes=native_share>context.control_margin?
            native_share-context.control_margin:1;
    }
    return result;
}
// Memory is shared by the actual heterogeneous family. Cost and lifetime stay
// attached to each row; a long preview is not repeated for every short profile.
inline std::size_t burst_amplitude_family_bank_count(std::span<const modem::Config> profiles,
        const Settings& value,ReceiverAdmissionContext context) {
    context.amplitude_family_bank_count.reset();context.key_count=context.profile_count=1;
    std::size_t total=0;
    for(auto config:profiles) {
        if(!config.symbol_burst.enabled())throw Error("Burst family contains an ordinary profile");
        const modem::SymbolBurstPlan plan(config);
        const auto first=plan.first_slot({static_cast<std::uint64_t>(std::max(0.,context.now)),0});
        config.stream_epoch=first.payload_begin.epoch;config.stream_phase_samples=first.payload_begin.phase;
        context.keyed=config.data_key.has_value();
        context.compact_clock_search=context.keyed&&modem::symbol_sample_count(config)>=60ULL*config.sample_rate;
        context.control_margin=receiver_control_margin(config,context.keyed);
        const auto prepared=prepare_receiver_search(config,value,config.stream_epoch,context);
        const auto count=prepared.search.amplitude_receiver_bank_count?
            prepared.search.amplitude_receiver_bank_count:modem::receiver_strategy_amplitude_bank_count(config,prepared.search,1,1);
        if(count>std::numeric_limits<std::size_t>::max()-total)throw Error("Burst family count overflow");
        total+=count;
    }
    return std::max<std::size_t>(1,total);
}

} // namespace datapump::live::detail
