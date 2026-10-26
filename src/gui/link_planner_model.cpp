#include "../scheduled_acquisition_policy.hpp"
#include "../live_helper.hpp"
#include "../live_helper_prior.hpp"
#include "../live_helper_frontend.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include "link_planner_model.hpp"
#include "../estimate_cancellation.hpp"
#include "datapump/execution.hpp"
#include "datapump/lpi_estimate.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/helper_assistance.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/sched_hop.hpp"
#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numeric>
#include <sstream>
#include <set>
#include <tuple>

namespace datapump::gui::planner {
namespace {
constexpr double seconds_per_day=86400;
constexpr std::size_t curve_samples=181;

bool automatic(tuning::PatternMode mode) {
    return mode==tuning::PatternMode::auto_pattern ||
        mode==tuning::PatternMode::auto_keystream || mode==tuning::PatternMode::auto_tone;
}
modem::Config resolve(const Inputs& inputs,double target) {
    const std::array targets{target};
    auto config=inputs.options.modem;
    // Scheduled geometry can construct the keyed packed-symbol calendar before
    // transfer::seeded_config runs. Use the same authoritative immutable key
    // as transfer::effective_options, clearing a stale Config-only key too.
    if(config.symbol_burst.enabled()||(config.scheduled_hopping&&config.scheduled_hopping->enabled()))
        config.data_key=inputs.options.key;
    return tuning::receive_profiles(config,targets,inputs.mode,
                                    inputs.options.key.has_value()).front();
}
double seconds(const modem::Config& config) {
    // Source-symbol integration stays unchanged; this quantity is the mean
    // wall time per bit used by the airtime graph and practical-rate buttons.
    if(config.symbol_burst.enabled()) {
        const modem::SymbolBurstPlan plan(config);
        long double total=0;
        for(unsigned extra=0;extra<=plan.random_limit();++extra)
            total+=plan.spacing_samples_for_additional_wait(extra);
        return static_cast<double>(total/(plan.random_limit()+1)/config.sample_rate);
    }
    if(modem::scheduled_bursts(config))return 1/modem::bit_rate(config);
    return static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate;
}
std::optional<modem::Config> try_resolve(const Inputs& inputs,double target) {
    try {
        auto config=resolve(inputs,target);
        // The graph probes much weaker targets than the selected setting.
        // A representable core can still have an unrepresentable Burst gap.
        if(config.symbol_burst.enabled())(void)seconds(config);
        return config;
    } catch(const Error&) { return std::nullopt; }
}
struct ReceiverSupport {
    bool clock=false,workspace=false;
    bool fits() const {return clock&&workspace;}
};
using SupportEntries=std::map<std::pair<std::uint64_t,double>,ReceiverSupport>;
struct SupportCache {
    SupportEntries entries;
    std::size_t evaluations=0,reuses=0;
};
bool search_supported(const simulation::Estimate& estimate) {
    return estimate.carrier_in_search&&estimate.clock_in_search&&!estimate.oscillator_search_limited;
}
simulation::Estimate receiver_estimate(const transfer::Estimate& transmission,const transfer::Options& options,
        bool raw,const modem::ChannelConfig& channel,std::span<const modem::Config> profiles,std::size_t keys,
        bool probability,double window,std::size_t trials,simulation::ReceiverWorkMode mode,
        const simulation::ReceiverTimingModel& timing,const live::Settings* settings,std::stop_token stop) {
    return settings?simulation::estimate_with_receiver_settings(transmission,options,raw,channel,*settings,
        profiles,keys,probability,window,trials,mode,timing,stop):
        simulation::estimate(transmission,options,raw,channel,profiles,keys,probability,window,trials,mode,timing,stop);
}
const live::Settings* receiver_settings(const Inputs& inputs) {
    return inputs.receiver_settings?&*inputs.receiver_settings:nullptr;
}
void helper_reference_plan(Model& model,const modem::Config& config) {
    if(!model.inputs.receiver_settings || model.inputs.receiver_settings->helper.power_w==0)return;
    auto& result=model.helper_reference;result.enabled=true;
    auto settings=*model.inputs.receiver_settings;
    // The planner's editable power is authoritative for this hypothetical
    // link. A retained Live snapshot can still contain the previous value.
    settings.helper.data_power_w=std::pow(10.,(model.inputs.tx_dbm-30)/10);
    settings.transfer.modem=config;
    if(settings.transfer.key)settings.helper.callsign.clear();
    try {
        helper::validate(settings.helper);
        result.effective_power_w=std::min(settings.helper.power_w,
            settings.helper.data_power_w*std::pow(10.,settings.helper.ratio_limit_db/10));
        result.received_dbm=30+10*std::log10(result.effective_power_w)-model.inputs.path_loss_db;
        result.cn0_db_hz=result.received_dbm-model.inputs.noise_density_dbm_hz;
        result.tone_snr_db=result.cn0_db_hz-10; // The tracker uses a 10 Hz cell.
        if(!std::isfinite(result.received_dbm)||!std::isfinite(result.cn0_db_hz)||!std::isfinite(result.tone_snr_db))
            throw Error("helper link budget exceeds the numerical range");
        result.budget_available=true;
        const auto waveform_gain=modem::outer_dsss_transmit_gain(config);
        result.simulation_level_modeled=model.inputs.receiver_work_mode==simulation::ReceiverWorkMode::sampled_simulation&&
            waveform_gain!=1;
        if(result.simulation_level_modeled) {
            result.simulation_level_db=20*std::log10(waveform_gain);
            result.simulation_data_cn0_db_hz=model.actual_cn0_db_hz+result.simulation_level_db;
            result.simulation_cn0_db_hz=result.cn0_db_hz+result.simulation_level_db;
            result.simulation_tone_snr_db=result.tone_snr_db+result.simulation_level_db;
        }
        result.tone_level_sufficient=(result.simulation_level_modeled?
            result.simulation_tone_snr_db:result.tone_snr_db)>=6;
        // This is the same placement as Live, without claiming that an unknown
        // hardware output rate or radio filter can pass the resulting tone.
        const auto placement=helper::placement_band(config,live::detail::helper_occupied_bandwidth(settings));
        const auto mono_passband=helper::mono_passband(placement);
        const auto plan=helper::plan(settings.helper,placement,120000000,0,waveform_gain);
        result.frequency_hz=plan.frequency_hz;result.geometry_available=true;
        // Apply exactly the ordinary catalogue-cell selector used by Live.
        // This is an assumed qualified measurement, never inferred from power.
        modem::OscillatorPatternSearch domain;
        if(config.oscillator_search)domain=modem::oscillator_pattern_search(config,
            transfer::effective_clock_sync(settings.transfer)?modem::utc_transmit_rate_limit(config):0);
        else domain.hypotheses={{0,0}};
        const auto conditional=live::detail::helper_prior_mask(domain.hypotheses,
            result.assumed_helper_offset_hz,result.assumed_frequency_uncertainty_hz,
            config.carrier_hz-plan.frequency_hz,
            .125*config.sample_rate/static_cast<double>(modem::symbol_sample_count(config)),domain.clock_step_ppm/2,
            domain.requested_clock_half_width_ppm);
        result.ordinary_pairs=domain.hypotheses.size();
        result.conditional_pairs=conditional.selected;
        result.retained_clock_rates=conditional.represented_rates;
        result.original_clock_cell_half_width_ppm=domain.clock_step_ppm/2;
        result.ordinary_rf_half_width_hz=domain.frequency.half_width_hz;
        result.requested_rf_half_width_hz=domain.frequency.requested_half_width_hz;
        result.baseline_search_limited=domain.limited||domain.frequency.limited;
        std::vector<double> carriers,selected_carriers;
        carriers.reserve(domain.hypotheses.size());selected_carriers.reserve(conditional.selected);
        for(std::size_t i=0;i<domain.hypotheses.size();++i) {
            const auto frequency=domain.hypotheses[i].frequency_offset_hz;
            carriers.push_back(frequency);
            if(conditional.cells[i])selected_carriers.push_back(frequency);
        }
        const auto unique_count=[](auto& values) {
            std::sort(values.begin(),values.end());
            return static_cast<std::size_t>(std::unique(values.begin(),values.end())-values.begin());
        };
        result.ordinary_carriers=unique_count(carriers);
        result.conditional_carriers=unique_count(selected_carriers);
        const auto clock_bound=domain.requested_clock_half_width_ppm*1e-6;
        const auto half_band=helper::occupied_bandwidth(config)/2;
        const auto helper_uncertainty=domain.frequency.requested_half_width_hz+std::abs(plan.frequency_hz)*clock_bound;
        const auto payload_uncertainty=domain.frequency.requested_half_width_hz+
            (std::abs(config.carrier_hz)+half_band)*clock_bound;
        result.helper_search_lower_hz=plan.frequency_hz-helper_uncertainty;
        result.helper_search_upper_hz=plan.frequency_hz+helper_uncertainty;
        result.payload_search_lower_hz=config.carrier_hz-half_band-payload_uncertainty;
        result.payload_search_upper_hz=config.carrier_hz+half_band+payload_uncertainty;
        // An engineering gate skirt may legitimately fold around DC without
        // losing the positive nominal waveform. Coarse conversion is required
        // for the uncertain nominal core or an upper capture edge, not for
        // that already represented real-mono gate image alone.
        const auto core_half=placement.waveform_bandwidth_hz/2;
        const auto core_uncertainty=domain.frequency.requested_half_width_hz+
            (std::abs(config.carrier_hz)+core_half)*clock_bound;
        const auto upper_uncertainty=domain.frequency.requested_half_width_hz+mono_passband.upper_hz*clock_bound;
        result.coarse_conversion_required=config.carrier_hz-core_half-core_uncertainty<=0||
            mono_passband.upper_hz+upper_uncertainty>=config.sample_rate/2.||
            result.helper_search_lower_hz<=0||result.helper_search_upper_hz>=config.sample_rate/2.;
        const auto symbol=modem::symbol_sample_count(config);
        result.broad_probe_eligibility_seconds=static_cast<double>(live::detail::helper_audit_period(config.sample_rate,symbol))/config.sample_rate;
        result.physical_symbol_seconds=static_cast<double>(symbol)/config.sample_rate;
        result.arrival_cell_seconds=model.receiver_geometry.timing_grid_seconds;
        if(result.arrival_cell_seconds>=result.punctuation_period_seconds)
            result.timing_status="The 0.4 s punctuation aliases intersect every arrival cell; no timing-count reduction is justified. All clock, epoch and keyed phase alternatives remain.";
        else result.timing_status="Independently qualified punctuation may narrow arrival cells while retaining every 0.4 s alias. No timing-count reduction is assumed here; clock, epoch and keyed phase alternatives remain.";
        const bool split=config.symbol_burst.enabled()||modem::scheduled_bursts(config);
        const bool policy=config.receiver_strategy==modem::ReceiverStrategy::automatic||
            config.receiver_strategy==modem::ReceiverStrategy::complex_only;
        const bool compact_requested=settings.transfer.key && symbol>=60ULL*config.sample_rate;
        result.compact_geometry=model.receiver_geometry.backend=="Compact direct correlation";
        result.carrier_projection_counts_available=result.compact_geometry&&config.spreading_mode==modem::SpreadingMode::pattern;
        bool native_filter=false;
        std::string frontend_limit;
        try {
            // The actual FIR constructor enforces native Nyquist and its finite
            // tap limit. Private cohort control and all-profile response bounds
            // remain runtime qualifications; do not copy an ABI byte estimate.
            const helper::PayloadFilter filter(config.sample_rate,plan.frequency_hz);
            native_filter=filter.enabled();
        }catch(const Error& error) {frontend_limit=error.what();}
        if(settings.helper.frequency==helper::Frequency::cochannel)
            result.status="Co-channel helper reception has no isolated common-link acquisition reference.";
        else if(split)
            result.status="Helper transmission and presence tracking are available; common-link acquisition is unavailable for this scheduled geometry.";
        else if(!policy)
            result.status="Presence tracking is available; the selected receiver strategy does not support common-link helper acquisition.";
        else if(!native_filter)
            result.status="Helper acquisition is unavailable in the native receiver/FIR passband: "+frontend_limit+".";
        else if(settings.transfer.timestamp)
            result.status="Fixed-epoch reception retains its separate legacy helper path; the common-link original-grid estimate does not apply to that path.";
        else if(compact_requested&&!result.compact_geometry)
            result.status="The long keyed profile has no resolved supported compact acquisition geometry in this plan; actual compact capability must be checked at runtime before conditional work is claimed.";
        else if(result.compact_geometry&&config.spreading_mode!=modem::SpreadingMode::pattern)
            result.status="The selected compact mode has no coherent pattern acquisition controls; physical projection work and conditional helper narrowing are not quantified.";
        else if(result.coarse_conversion_required) {
            result.status="The declared RF/clock domain requires coarse discovery and conversion before narrow receiver processing. ";
            result.status+=config.oscillator_search && config.oscillator_search->reference==modem::OscillatorReference::shared_radio?
                "The clipped ordinary LF catalogue cannot describe the new shared-clock conditional catalogue; its work is not estimated here. ":
                "Post-conversion conditional work is not estimated here. ";
            result.status+="Raw capture, signed-image branches, DC/image rejection and the radio passband require runtime qualification.";
        }
        else if(!conditional.admitted)
            result.status="Under the assumed qualified helper measurement, "+std::string(conditional.reason)+". A nonzero RF origin can align a full catalogue without reducing pair work.";
        else {
            result.conditional_geometry_available=true;
            result.status="Normal Automatic or Complex reception with rolling epoch admission can use conditional common-link original-grid acquisition with qualification gates and optional future broad probes. ";
            result.status+=modem::deferred_symbols(config)?
                "Deferred FHSS can retain the ordinary native calendar path with conditional RF controls; calendar timing, unresolved LF and keyed phase alternatives remain. ":result.compact_geometry?
                "Compact carrier projections and cold arrival-lane accumulation can be omitted outside the qualified RF strip; origin zero and the complete unresolved LF domain are retained. ":
                "Native conditional FFT work retains the complete unresolved LF domain. ";
            result.status+="Actual backend capability, all-profile FIR response and reserved-workspace qualification remain unverified until runtime.";
            result.status+=" One global cold probe slot rotates current key/profile/epoch ranks; its original RF/LF search is partial future coverage, not an audit of the complete cohort. Optional work is limited by credit earned from actual conditional execution, with at most one indivisible packet of excess work. Any payload dependency promotes that owner to ordinary work outside this optional budget. Helper loss or inconsistency immediately restores broad future acquisition for every current key, profile and epoch.";
        }
        result.status+=" Conditional geometry assumes a qualified frequency confidence interval of +/-"+
            std::to_string(result.assumed_frequency_uncertainty_hz)+" Hz at helper offset "+
            std::to_string(result.assumed_helper_offset_hz)+" Hz; predicted SNR does not qualify that measurement.";
        result.status+=" Frequency narrowing uses the strongest independently qualified stable tone in the expected range; phase, punctuation and metadata decoding are not required. Phase assistance has a separate measured qualification and no modeled gain here.";
        if(settings.helper.frequency==helper::Frequency::low)
            result.status+=" Low placement leaves the adjacent helper in the ordinary capture band; interference isolation is unqualified.";
        else if(settings.helper.frequency==helper::Frequency::cochannel)
            result.status+=" Co-channel placement overlaps the payload and can interfere; this option is discouraged.";
    } catch(const Error& error) {
        result.status="Helper unavailable: "+std::string(error.what());
    }
}
auto planning_policy_key() {
    const auto* batching=std::getenv("DATAPUMP_SCHED_LAZY_BATCH");
    std::optional<std::size_t> samples;bool invalid=false;
    if(const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES")) {
        char* end=nullptr;const auto count=std::strtoull(value,&end,10);
        invalid=end==value||*end!='\0'||count<1||count>8192;
        if(!invalid)samples=static_cast<std::size_t>(count);
    }
    return std::tuple{modem::detail::scheduled_acquisition_policy::mode(),batching&&std::string_view(batching)=="0",samples,invalid};
}
simulation::ReceiverWorkMode support_work_mode(const Inputs& inputs) {
    // Ordinary full-bank fit does not require arrival enumeration. Scheduled
    // layout is calendar-aware: preserve a genuinely qualified timing prior
    // instead of replacing it with an ownerless full-window fallback.
    if(inputs.options.modem.symbol_burst.enabled()||inputs.options.modem.fake_hopping||
       (inputs.options.modem.scheduled_hopping&&inputs.options.modem.scheduled_hopping->enabled()))
        return inputs.receiver_work_mode;
    return inputs.receiver_work_mode==simulation::ReceiverWorkMode::hardware_timing_model?
        simulation::ReceiverWorkMode::hardware_fallback:inputs.receiver_work_mode;
}
ReceiverSupport receiver_support(const Inputs& inputs,const modem::Config& config,
        std::span<const modem::Config> profiles={},std::stop_token stop={}) {
    estimate_detail::check(stop);
    auto options=inputs.options;options.modem=config;
    auto channel=inputs.channel;
    channel.snr_db=std::clamp(inputs.tx_dbm-inputs.path_loss_db-inputs.noise_density_dbm_hz-
        10*std::log10(config.sample_rate/2.),-300.,300.);
    // Workspace and clock coverage depend on the selected geometry and local
    // banks, not payload length. This analytical one-symbol estimate allocates
    // neither a draft, a transfer probe, nor PCM inside the bounded search.
    transfer::Estimate one;one.wire_bits=1;one.total_seconds=seconds(config);
    try {
        const auto estimate=simulation::estimate_receiver_support(one,options,channel,profiles,1,
            support_work_mode(inputs),(config.symbol_burst.enabled()||config.fake_hopping||(config.scheduled_hopping&&config.scheduled_hopping->enabled()))?
                inputs.receiver_timing_model:simulation::ReceiverTimingModel{},
            receiver_settings(inputs),stop);
        return {search_supported(estimate),estimate.receiver_workspace_supported};
    } catch(const Error&) {
        // A checked geometry/address overflow at an illustrative target is a support
        // gap. The selected target is still estimated directly and reports its
        // own error; do not erase it because a graph extreme is unrepresentable.
        return {};
    }
}

// All searches have fixed iteration counts. Preserve discrete tuner steps:
// a boundary is the weakest target whose actual sampled duration meets it.
std::optional<double> duration_boundary(const Inputs& inputs,double minimum,double maximum,double limit,std::stop_token stop={}) {
    const auto weak=try_resolve(inputs,minimum),strong=try_resolve(inputs,maximum);
    if(!weak || !strong || seconds(*weak)<=limit || seconds(*strong)>limit)return std::nullopt;
    for(unsigned i=0;i<56;++i) {
        estimate_detail::check(stop);
        const auto middle=(minimum+maximum)/2;
        const auto config=try_resolve(inputs,middle);
        if(config && seconds(*config)<=limit)maximum=middle;
        else minimum=middle;
    }
    return maximum;
}
struct ClockBoundary {double target;std::string reason;};
struct ClockCandidate {double target;modem::Config config;};
std::optional<ClockCandidate> sample_candidate(const Inputs& inputs,std::uint64_t samples) {
    if(samples<4)return std::nullopt;
    const auto duration=(static_cast<long double>(samples)-.5L)/inputs.options.modem.sample_rate;
    auto target=tuning::pattern_target_symbol_snr_db-10*std::log10(static_cast<double>(duration));
    // Center the target inside one sample's rounding interval, then verify the
    // tuner really selected that count. A few adjacent doubles cover roundoff
    // near long durations; never publish a merely approximate sample match.
    for(unsigned attempt=0;attempt<8;++attempt) {
        if(target< -200 || target>200)return std::nullopt;
        auto config=try_resolve(inputs,target);
        if(!config)return std::nullopt;
        const auto actual=modem::symbol_sample_count(*config);
        if(actual==samples)return ClockCandidate{target,std::move(*config)};
        if(!config->integration_seconds)return std::nullopt;
        target=std::nextafter(target,actual>samples?std::numeric_limits<double>::infinity():
            -std::numeric_limits<double>::infinity());
    }
    return std::nullopt;
}
std::optional<ClockCandidate> clock_candidate(const Inputs& inputs,double target) {
    auto config=try_resolve(inputs,target);
    if(!config)return std::nullopt;
    if(config->integration_seconds>0) {
        // Adjacent sampled durations can use different projection bins. Seek
        // complete native bins so a RAM milestone does not stop arbitrarily
        // at an odd-sample gap while substantially longer symbols still fit.
        // This selects a real, slightly stronger tuner target; it does not
        // change the modem's sample rounding or its workspace model.
        const auto chip=modem::pattern_chip_samples(*config);
        const auto alignment=std::gcd(chip,std::max<std::uint64_t>(1,chip/2));
        const auto symbol=modem::symbol_sample_count(*config);
        auto aligned=symbol-symbol%alignment;
        if(aligned>alignment && (static_cast<long double>(aligned)-.5L)/config->sample_rate>
                config->integration_seconds)aligned-=alignment;
        if(aligned>=4) {
            auto candidate=sample_candidate(inputs,aligned);
            if(candidate && candidate->target>=target)return candidate;
        }
    }
    return ClockCandidate{target,std::move(*config)};
}
struct ClockChoices {
    std::optional<ClockBoundary> boundary;
    std::optional<double> stronger,weaker;
};
class ClockSearch {
    const Inputs& inputs;
    double minimum,maximum;
    std::uint64_t alignment;
    std::vector<std::uint64_t> divisors;
    std::vector<double> targets;
    SupportEntries support_cache;
    SupportCache* persistent_support;
    std::span<const double> companion_targets;
    std::optional<ReceiveBanks> receive_banks;
    std::stop_token stop;

    void add(double target) {
        if(std::isfinite(target)&&target>=minimum&&target<=maximum)targets.push_back(target);
    }
    void add_sample(std::uint64_t samples) {
        if(const auto candidate=sample_candidate(inputs,samples))add(candidate->target);
    }
    void near_samples(long double value,bool all_divisors=false) {
        if(value<4 || value>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())-alignment)return;
        const auto samples=static_cast<std::uint64_t>(value);
        for(const auto sample:{samples-1,samples,samples+1})add_sample(sample);
        const auto adjacent=[&](std::uint64_t divisor) {
            const auto lower=samples-samples%divisor;
            add_sample(lower);add_sample(lower+divisor);
        };
        if(all_divisors)for(const auto divisor:divisors)adjacent(divisor);
        else adjacent(alignment);
    }
    void near_target(double target,bool all_divisors=false) {
        add(target);
        if(const auto config=try_resolve(inputs,std::clamp(target,minimum,maximum));
           config&&config->integration_seconds>0)
            near_samples(modem::symbol_sample_count(*config),all_divisors);
    }
    ReceiverSupport check(double target) {
        estimate_detail::check(stop);
        const auto config=try_resolve(inputs,target);
        if(!config)return {};
        const auto samples=modem::symbol_sample_count(*config);
        // Mixed public/private families can cross different short-profile
        // floors at the same selected sample count, so retain target identity
        // when an explicit receive-bank context is supplied.
        const auto cache_key=std::pair{samples,receive_banks?target:0.};
        if(persistent_support) {
            const auto found=persistent_support->entries.find(cache_key);
            if(found!=persistent_support->entries.end()) {
                ++persistent_support->reuses;return found->second;
            }
        }
        if(const auto found=support_cache.find(cache_key);found!=support_cache.end())return found->second;
        std::vector<modem::Config> profiles;
        if(!companion_targets.empty()||receive_banks) {
            std::vector<double> combined(companion_targets.begin(),companion_targets.end());
            combined.push_back(target);
            const auto append=[&](bool keyed,std::size_t count) {
                if(!count)return;
                const auto family=tuning::receive_profiles(inputs.options.modem,combined,inputs.mode,keyed);
                for(std::size_t key=0;key<count;++key)profiles.insert(profiles.end(),family.begin(),family.end());
            };
            if(receive_banks) {
                append(false,receive_banks->plaintext?1:0);
                append(true,receive_banks->private_keys);
                if(profiles.empty())return {};
            } else append(inputs.options.key.has_value(),1);
        }
        const auto support=receiver_support(inputs,*config,profiles,stop);
        if(persistent_support) {
            ++persistent_support->evaluations;
            // Preserve the common scan entries at capacity. The per-run map
            // still avoids repeated work for any additional sample counts.
            if(persistent_support->entries.size()<8192)
                persistent_support->entries.emplace(cache_key,support);
        }
        return support_cache.emplace(cache_key,support).first->second;
    }
    void sort_targets() {
        std::sort(targets.begin(),targets.end());
        targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
    }
    // Refine a known fitting endpoint without assuming all profiles inside a
    // bracket are usable. Every retained endpoint is checked by the estimator.
    double refine(double failed,double fitting) {
        for(unsigned i=0;i<48;++i) {
            const auto middle=(failed+fitting)/2;
            const auto candidate=clock_candidate(inputs,middle);
            if(!candidate || candidate->target<=std::min(failed,fitting) ||
               candidate->target>=std::max(failed,fitting))break;
            if(check(candidate->target).fits())fitting=candidate->target;
            else failed=candidate->target;
        }
        return fitting;
    }
    std::optional<double> step(bool stronger) {
        constexpr double direction_tolerance=1e-8;
        const auto current=inputs.target_db_hz;
        const auto desired=std::clamp(current+(stronger?1:-1),minimum,maximum);
        const auto correct_direction=[&](double target) {
            return stronger?target>current+direction_tolerance:target<current-direction_tolerance;
        };
        if(!correct_direction(desired))return std::nullopt;
        if(check(desired).fits())return desired;
        std::optional<double> beyond,remaining;
        for(const auto target:targets) {
            estimate_detail::check(stop);
            if(!correct_direction(target)||!check(target).fits())continue;
            if(stronger?target>=desired:target<=desired) {
                if(!beyond || (stronger?target<*beyond:target>*beyond))beyond=target;
            } else if(!remaining || (stronger?target>*remaining:target<*remaining))remaining=target;
        }
        // Normally move about 1 dB, continuing across unsupported regions.
        // Only a last checked edge may shorten the remaining step.
        auto result=beyond?beyond:remaining;
        if(beyond&&std::abs(*beyond-desired)>.001)result=refine(desired,*beyond);
        if(result&&correct_direction(*result)&&check(*result).fits())return result;
        return std::nullopt;
    }
public:
    ClockSearch(const Inputs& value,double low,double high,
            std::span<const double> companions={},std::optional<ReceiveBanks> banks=std::nullopt,
            SupportCache* persistent=nullptr,std::stop_token cancel={}):
        inputs(value),minimum(low),maximum(high),
        persistent_support(companions.empty()&&!banks?persistent:nullptr),
        companion_targets(companions),receive_banks(banks),stop(cancel) {
        estimate_detail::check(stop);
        const auto chip=modem::pattern_chip_samples(inputs.options.modem);
        alignment=std::gcd(chip,std::max<std::uint64_t>(1,chip/2));
        // Divisor work is bounded by the validated rate/sample-clock geometry,
        // never by the number of bits or their simulated duration.
        for(std::uint64_t divisor=1;divisor<=alignment/divisor;++divisor) {
            estimate_detail::check(stop);
            if(alignment%divisor!=0)continue;
            divisors.push_back(divisor);
            if(divisor!=alignment/divisor)divisors.push_back(alignment/divisor);
        }
        std::sort(divisors.begin(),divisors.end());
    }
    ClockChoices run() {
        constexpr unsigned cover_steps=128;
        for(unsigned i=0;i<=cover_steps;++i)near_target(minimum+(maximum-minimum)*i/cover_steps);
        near_target(inputs.target_db_hz,true);
        near_target(inputs.target_db_hz+1,true);near_target(inputs.target_db_hz-1,true);
        const auto fs=static_cast<long double>(inputs.options.modem.sample_rate);
        const auto& base=inputs.options.modem;
        for(const auto duration:{16.L,60.L})near_samples(duration*fs);
        // Named automatic profiles and confidence floors introduce their own
        // target steps. The tuner remains authoritative at every candidate.
        for(unsigned factor=16;factor<=16384;factor*=2) {
            const auto target=tuning::pattern_target_symbol_snr_db-10*std::log10(2.*factor/base.bandwidth_hz);
            for(const auto value:{std::nextafter(target,-std::numeric_limits<double>::infinity()),target,
                    std::nextafter(target,std::numeric_limits<double>::infinity())})near_target(value);
        }
        for(const auto floor:{24.,30.})near_target(floor+10*std::log10(base.bandwidth_hz));
        const auto offset=std::abs(static_cast<long double>(inputs.channel.frequency_offset_hz)+
            base.carrier_hz*static_cast<long double>(inputs.channel.clock_error_ppm)*1e-6L);
        constexpr auto bank_steps=(modem::maximum_pattern_frequency_hypotheses-1)/2;
        if(offset>0) {
            near_samples(.5L*fs/offset,true);
            near_samples(.25L*bank_steps*fs/offset,true);
        }
        // FFT powers and the compact-transform 4/5 boundary are candidate
        // events, not grounds for assuming memory use is monotonic.
        for(unsigned power=2;power<64;++power) {
            const auto samples=std::ldexp(static_cast<long double>(alignment),static_cast<int>(power));
            near_samples(samples);near_samples(.8L*samples);near_samples(samples-4*alignment);
        }
        if(!tuning::tone_mode(inputs.mode)) {
            const auto reference=clock_candidate(inputs,std::clamp(-40.,minimum,maximum));
            if(reference) {
                const auto requested=base.oscillator_search?
                    static_cast<long double>(modem::oscillator_pattern_search(reference->config,
                        transfer::effective_clock_sync(inputs.options)&&inputs.receiver_work_mode!=simulation::ReceiverWorkMode::sampled_simulation?
                        modem::utc_transmit_rate_limit(reference->config):0).frequency.requested_half_width_hz):
                    base.carrier_hz*modem::default_clock_uncertainty_ppm*1e-6L;
                const auto headroom=static_cast<long double>(modem::pattern_frequency_offset_limit(reference->config));
                const auto span=std::min(requested,headroom);
                if(span>0) {
                    near_samples(bank_steps*fs/(4*span));
                    const bool ceil_bank=headroom>requested;
                    for(const auto divisor:divisors) {
                        near_samples(static_cast<long double>(bank_steps)*divisor);
                        const auto ratio=4*span*divisor/fs;
                        if(ceil_bank?ratio>1:ratio<1)continue;
                        const auto difference=std::abs(1-ratio);
                        const auto count=difference==0?bank_steps:static_cast<std::size_t>(
                            std::min(static_cast<long double>(bank_steps),std::ceil(1/difference)+1));
                        for(std::size_t step=3;step<=count;++step) {
                            near_samples(static_cast<long double>(step)*divisor);
                            near_samples(step*fs/(4*span));
                        }
                    }
                    // A clock just outside requested coverage can fit the
                    // small overshoot at an uncapped ceil-bank transition.
                    if(ceil_bank&&offset>span&&offset<=headroom) {
                        const auto count=static_cast<std::size_t>(std::min(static_cast<long double>(bank_steps),
                            std::ceil(offset/(offset-span))+1));
                        for(std::size_t step=3;step<=count;++step)near_samples(step*fs/(4*span));
                    }
                }
            }
        }
        sort_targets();
        ClockChoices result;
        auto first=std::find_if(targets.begin(),targets.end(),[&](double target){return check(target).fits();});
        if(first!=targets.end()) {
            auto edge=*first;
            if(edge>minimum+1e-8) {
                // Include the old coarse bracket's useful extent, then probe
                // all divisor grids around the retained endpoint. This also
                // finds terminal islands finer than the full native-bin grid.
                const auto failed=std::max(minimum,edge-(maximum-minimum)/cover_steps);
                if(!check(failed).fits())edge=refine(failed,edge);
                if(const auto config=try_resolve(inputs,edge))near_samples(modem::symbol_sample_count(*config),true);
                add(edge);sort_targets();
                first=std::find_if(targets.begin(),targets.end(),[&](double target){return check(target).fits();});
                edge=*first;
                const auto config=try_resolve(inputs,edge);
                auto failed_support=ReceiverSupport{};
                if(config) {
                    const auto samples=modem::symbol_sample_count(*config);
                    const auto next=samples-samples%alignment+alignment;
                    if(const auto weaker=sample_candidate(inputs,next))failed_support=check(weaker->target);
                }
                const auto reason=!failed_support.clock?(!failed_support.workspace?"Clock + RAM":"Clock"):
                    !failed_support.workspace?"RAM":"Clock / RAM gaps";
                result.boundary=ClockBoundary{edge,reason};
            }
        }
        result.stronger=step(true);result.weaker=step(false);
        return result;
    }
    std::optional<double> nearest() {
        const auto requested=inputs.target_db_hz;
        if(requested>=minimum&&requested<=maximum&&check(requested).fits())return requested;
        if(!automatic(inputs.mode))return std::nullopt;
        (void)run();
        const auto choose=[&]() -> std::optional<double> {
            const auto split=std::upper_bound(targets.begin(),targets.end(),requested);
            for(auto candidate=split;candidate!=targets.begin();) {
                --candidate;
                if(check(*candidate).fits())return *candidate;
            }
            for(auto candidate=split;candidate!=targets.end();++candidate)
                if(check(*candidate).fits())return *candidate;
            return std::nullopt;
        };
        auto result=choose();
        if(!result)return std::nullopt;
        // Refine larger coverage gaps, then inspect both sides of every local
        // divisor grid. The retained target always resolves to a checked
        // sample count; nominal decimal rounding cannot recreate a RAM gap.
        if(std::abs(*result-requested)>.001) {
            const auto edge=refine(std::clamp(requested,minimum,maximum),*result);
            add(edge);
            if(const auto config=try_resolve(inputs,edge))
                near_samples(modem::symbol_sample_count(*config),true);
            sort_targets();result=choose();
        }
        return result;
    }
};
std::string number(double value,int precision=3) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(precision)<<value;
    return out.str();
}
std::uint64_t add_samples(std::uint64_t first,std::uint64_t second) {
    if(second>std::numeric_limits<std::uint64_t>::max()-first)
        throw Error("planned duration exceeds the modem sample counter");
    return first+second;
}
transfer::Estimate one_bit_estimate(const transfer::Options& options) {
    const auto config=modem::scheduled_estimate_config(
        options.key?transfer::seeded_config(options,options.timestamp):options.modem);
    const auto symbol=modem::symbol_sample_count(config);
    auto count=add_samples(modem::training_sample_count(config),symbol);
    count=add_samples(count,modem::scheduled_symbol_offset(config,config.stream_epoch,config.stream_phase_samples,0));
    count=add_samples(count,modem::pattern_pulse_padding_samples(config));
    count=add_samples(count,modem::pattern_pulse_padding_samples(config));
    count=add_samples(count,modem::suppression_sample_count(config));
    if(config.symbol_burst.enabled()||modem::scheduled_bursts(config))count=modem::pattern_transmission_sample_count(config,1);
    transfer::Estimate result;
    result.wire_bits=result.coded_bytes=result.content_bytes=1;
    result.content_seconds=result.coded_seconds=seconds(config);
    result.total_seconds=static_cast<double>(count)/config.sample_rate;
    result.waveform_samples=count<=std::numeric_limits<std::size_t>::max()?static_cast<std::size_t>(count):0;
    return result;
}
std::uint64_t scheduled_content_samples(const modem::Config& config,std::uint64_t bits) {
    if(config.symbol_burst.enabled()) {
        if(!bits)return 0;
        const modem::SymbolBurstPlan plan(config);
        const auto first=plan.bind_payload(config.stream_epoch,config.stream_phase_samples);
        modem::SymbolBurstCursor cursor(plan,first);
        return add_samples(plan.distance(first.payload_begin,cursor.at(bits-1).payload_begin),plan.symbol_samples());
    }
    const modem::SchedHopSourceMap map(*config.scheduled_hopping,*config.data_key,config.sample_rate);
    const auto anchor=modem::symbol_schedule_detail::add(
        modem::symbol_schedule_detail::multiply(config.stream_epoch,config.sample_rate),config.stream_phase_samples);
    const auto end=map.source_to_physical(map.locate(anchor).source.add_samples(
        modem::symbol_schedule_detail::multiply(bits,modem::symbol_sample_count(config))),true);
    if(end.sample<anchor)throw Error("planned scheduled payload precedes its arrival anchor");
    return end.sample-anchor;
}
auto curve_base_key(const transfer::Options& options,const modem::ChannelConfig& channel,
               simulation::ReceiverWorkMode work_mode=simulation::ReceiverWorkMode::sampled_simulation,
               const simulation::ReceiverTimingModel& timing_model={}) {
    const auto c=options.key?transfer::seeded_config(options,options.modem.stream_epoch):options.modem;
    const auto oscillator=c.oscillator_search.value_or(modem::OscillatorSearchConfig{});
    // Only quantities read by the estimator enter this bounded cache. The
    // private key's derived pattern identity is retained; the sampled-channel
    // RNG seed does not choose probability draws.
    return std::tuple{c.pattern_symbols,c.stream_epoch,c.stream_phase_samples,c.sample_rate,
        c.constellation_bits,c.carrier_hz,c.bandwidth_hz,c.training_seconds,
        c.symbol_burst.wait_symbols,c.symbol_burst.enabled()&&options.key?c.symbol_burst.random_limit:0U,
        modem::symbol_sample_count(c),modem::pattern_chip_samples(c),c.spreading_mode,c.pulse_shaping,c.scramble,c.dsss,modem::legacy_public_pattern(c),
        c.dsss_factor,c.dsss_factor>1?c.outer_dsss_version:modem::OuterDsssVersion::legacy_v1,c.search_arithmetic,c.receiver_strategy,c.receiver_cpu_budget,c.frequency_search_multiplier,c.spreading_seed,c.dsss_seed,c.memory_limit,options.dsp_workspace_bytes,
        options.timestamp,options.search_seconds,options.audio_timing_error_seconds,options.clock_sync.has_value(),
        options.clock_sync.value_or(clock_sync::Policy{}).accuracy_seconds,
        options.clock_sync.value_or(clock_sync::Policy{}).region_seconds,
        options.clock_sync.value_or(clock_sync::Policy{}).offset_seconds,channel.snr_db,channel.frequency_offset_hz,
        channel.delay_samples,channel.clock_error_ppm,channel.phase_noise_degrees_per_sqrt_second,
        c.fake_hopping?c.fake_hopping->power_ramps:false,modem::deferred_symbols(c),
        c.fake_hopping?c.fake_hopping->matched_gaps:false,c.fake_hopping?c.fake_hopping->mandatory_ramps:false,
        c.fake_hopping?c.fake_hopping->smooth_ramps:false,c.fake_hopping?c.fake_hopping->remote_guard_covered:false,
        c.fake_hopping?c.fake_hopping->ramp_seconds:0.,
        c.fake_hopping?c.fake_hopping->dwell_seconds:0.,
        c.fake_hopping?c.fake_hopping->clock_error_seconds:0.,
        c.fake_hopping?c.fake_hopping->audio_error_seconds:0.,
        c.fake_hopping?c.fake_hopping->peer_clock_error_seconds:0.,
        c.fake_hopping?c.fake_hopping->region_seconds:0.,
        c.fake_hopping?c.fake_hopping->offset_seconds:0.,
        bool(c.scheduled_hopping),
        c.scheduled_hopping?c.scheduled_hopping->time_multiplier:1U,
        c.scheduled_hopping?c.scheduled_hopping->group_hops:std::uint64_t{1},
        c.scheduled_hopping?c.scheduled_hopping->clock_error_seconds:0.,
        c.scheduled_hopping?c.scheduled_hopping->peer_clock_error_seconds:0.,
        c.scheduled_hopping?c.scheduled_hopping->region_seconds:0.,
        c.scheduled_hopping?c.scheduled_hopping->offset_seconds:0.,
        c.scheduled_hopping?c.scheduled_hopping->audio_error_seconds:0.,
        c.scheduled_hopping?c.scheduled_hopping->ramp_seconds:0.,
        c.oscillator_search.has_value(),oscillator.lf.accuracy_ppm,oscillator.lf.phase_noise_degrees_per_sqrt_second,
        oscillator.rf.accuracy_ppm,oscillator.rf.phase_noise_degrees_per_sqrt_second,
        oscillator.rf_shift_hz,oscillator.margin,oscillator.reference,oscillator.sideband,work_mode,
        timing_model.capture_error_seconds,timing_model.capture_rate_uncertainty_fraction,
        timing_model.capture_seconds_per_frame};
}
auto receiver_layout_key(const transfer::Options& options,const live::Settings* settings) {
    std::vector<Bytes> keys;
    const auto add=[&](const Crypto& key) {
        auto tag=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
        if(std::find(keys.begin(),keys.end(),tag)==keys.end())keys.push_back(std::move(tag));
    };
    if(settings) {
        if(settings->transfer.key)add(*settings->transfer.key);
        for(const auto& key:settings->receive_keys)add(key);
    }
    std::sort(keys.begin(),keys.end());
    return std::tuple{bool(settings),settings?live::receiver_workspace_capacity(*settings):std::size_t{0},
        live::receiver_control_margin(options.modem,options.key.has_value()),
        settings?settings->simulation:false,settings?settings->content_limit:std::size_t{0},
        settings?settings->transfer.content_limit:std::size_t{0},
        settings?settings->transfer.receive_targets_db_hz:std::vector<double>{},
        settings?settings->transfer.receive_pattern_mode:tuning::PatternMode::auto_pattern,
        settings?settings->transfer.automatic_receive_profiles:false,
        settings?settings->receive_keys.size():std::size_t{0},settings?settings->transfer.key.has_value():false,
        settings?curve_base_key(settings->transfer,modem::ChannelConfig{}):
            curve_base_key(transfer::Options{},modem::ChannelConfig{}),keys,planning_policy_key()};
}
auto curve_key(const transfer::Options& options,const modem::ChannelConfig& channel,
               simulation::ReceiverWorkMode work_mode=simulation::ReceiverWorkMode::sampled_simulation,
               const simulation::ReceiverTimingModel& timing_model={},const live::Settings* settings=nullptr) {
    return std::tuple{curve_base_key(options,channel,work_mode,timing_model),receiver_layout_key(options,settings)};
}
using CurveKey=decltype(curve_key(transfer::Options{},modem::ChannelConfig{}));
auto support_context(const Inputs& inputs) {
    // Target and payload length do not change support at a given sample count.
    // Explicit key presence selects the public/private profile family, beyond
    // the derived pattern identity already retained by curve_key.
    auto options=inputs.options;options.modem=resolve(inputs,200);
    return std::tuple{curve_key(options,inputs.channel,support_work_mode(inputs),
        (options.modem.symbol_burst.enabled()||modem::scheduled_bursts(options.modem))?inputs.receiver_timing_model:simulation::ReceiverTimingModel{},receiver_settings(inputs)),inputs.mode,
        inputs.options.key.has_value(),inputs.tx_dbm,inputs.path_loss_db,inputs.noise_density_dbm_hz};
}
using SupportContext=decltype(support_context(Inputs{}));
struct CurveEntry {
    CurveKey key;
    simulation::Estimate estimate;
    bool probability_computed=false;
};
struct CurveCache {
    std::vector<CurveEntry> entries;
    std::size_t probability_evaluations=0,probability_reuses=0;
};
CurveEntry& curve_entry(CurveCache& cache,const transfer::Options& options,const modem::ChannelConfig& channel,
                       simulation::ReceiverWorkMode work_mode,const simulation::ReceiverTimingModel& timing_model,const live::Settings* settings,std::stop_token stop) {
    estimate_detail::check(stop);
    auto& entries=cache.entries;
    const auto key=curve_key(options,channel,work_mode,timing_model,settings);
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.key==key;});
    if(found!=entries.end())return *found;
    if(entries.size()==512)entries.erase(entries.begin());
    simulation::Estimate estimate;
    try {
        estimate=receiver_estimate(one_bit_estimate(options),options,true,channel,{},1,false,100,4096,
            work_mode,timing_model,settings,stop);
    } catch(const Error&) {
        // An unrepresentable illustrative point must not erase a valid selection.
        estimate.carrier_in_search=estimate.clock_in_search=false;
        estimate.receiver_workspace_supported=estimate.receiver_work_supported=false;
    }
    entries.push_back({key,std::move(estimate),false});
    return entries.back();
}
ReceivePoint receive_point(double target,const simulation::Estimate& estimate,bool numerical_range) {
    ReceivePoint result;result.target_db_hz=target;result.success_probability=estimate.success_probability;
    result.confidence_available=estimate.confidence_available&&numerical_range;
    result.reference_available=estimate.reference_probability_available&&numerical_range;
    result.probability_reference_only=estimate.probability_reference_only;
    result.clock_supported=search_supported(estimate);result.workspace_supported=estimate.receiver_workspace_supported;
    result.probability_trials=estimate.probability_trials;return result;
}
double planned_cpu_ratio(const simulation::Estimate& estimate) {
    if(estimate.symbol_burst.modeled)return std::max(estimate.symbol_burst.finite_realtime_ratio,
        estimate.symbol_burst.tracked_realtime_ratio);
    const auto finite=estimate.simulated_seconds>0?estimate.receiver_cpu_seconds/estimate.simulated_seconds:0;
    return std::max(finite,estimate.hardware_sustained_modeled?estimate.hardware_sustained_realtime_ratio:0.);
}
CpuPoint cpu_point(double target,const simulation::Estimate& estimate) {
    const auto ratio=planned_cpu_ratio(estimate);
    return {target,std::isfinite(ratio)&&ratio>=0?ratio:0.,estimate.receiver_work_supported&&search_supported(estimate)&&estimate.receiver_workspace_supported&&
        std::isfinite(ratio)&&ratio>0};
}
void receive_curve(Model& model,const simulation::Estimate& selected_one,CurveCache& cache,std::stop_token stop) {
    if(model.points.empty())return;
    const auto& inputs=model.inputs;
    const auto low=model.points.front().target_db_hz,high=model.points.back().target_db_hz;
    constexpr unsigned expensive_limit=12,coarse_limit=6,refinement_limit=32;
    unsigned expensive=0;
    std::map<double,ReceivePoint> output;
    std::map<double,CpuPoint> cpu_output;
    const auto evaluate=[&](double target,bool permit_expensive) -> bool {
        estimate_detail::check(stop);
        if(target<low||target>high||output.contains(target))return false;
        const auto geometry=try_resolve(inputs,target);
        if(!geometry){
            output.emplace(target,ReceivePoint{target});cpu_output.emplace(target,CpuPoint{target});return true;
        }
        auto options=inputs.options;options.modem=*geometry;
        auto channel=inputs.channel;
        const auto sample_snr=model.actual_cn0_db_hz-10*std::log10(geometry->sample_rate/2.);
        const bool numerical_range=sample_snr>=-300&&sample_snr<=300;
        channel.snr_db=std::clamp(sample_snr,-300.,300.);
        auto& entry=curve_entry(cache,options,channel,inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop);
        // Receiver work is already available from the cheap support check;
        // the denser CPU curve adds no statistical trials or sampled audio.
        cpu_output.emplace(target,cpu_point(target,entry.estimate));
        if(!search_supported(entry.estimate)||!entry.estimate.receiver_workspace_supported||!numerical_range) {
            output.emplace(target,receive_point(target,entry.estimate,numerical_range));return true;
        }
        const bool costly=entry.estimate.drift_sections>1;
        if(costly&&(!permit_expensive||expensive>=expensive_limit))return false;
        // Count chosen expensive sample locations, including cache hits, so
        // identical inputs produce an identical curve on every repaint.
        if(costly)++expensive;
        if(!entry.probability_computed) {
            // Curve locations are illustrative; the selected estimate uses
            // 4096 draws and exposes its own sampling interval separately.
            entry.estimate=receiver_estimate(one_bit_estimate(options),options,true,channel,{},1,true,100,512,
                inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop);
            entry.probability_computed=true;
            ++cache.probability_evaluations;
        } else ++cache.probability_reuses;
        output.emplace(target,receive_point(target,entry.estimate,numerical_range));return true;
    };
    const auto probe=[&](double target,bool permit_expensive) {
        bool added=evaluate(target,permit_expensive);
        const auto exact=output.find(target);
        if(model.automatic_mode&&exact!=output.end()&&!exact->second.confidence_available&&!exact->second.reference_available) {
            // A decimal target can select an odd-sample RAM gap immediately
            // beside a useful aligned profile. Use that checked target when
            // the difference is below displayed precision, as the dropdown
            // does. Wider unavailable intervals remain explicit graph gaps.
            if(const auto aligned=clock_candidate(inputs,target);aligned&&aligned->target!=target) {
                added=evaluate(aligned->target,permit_expensive)||added;
                if(std::abs(aligned->target-target)<=.001&&target!=inputs.target_db_hz) {
                    auto options=inputs.options;options.modem=aligned->config;
                    auto channel=inputs.channel;
                    const auto snr=model.actual_cn0_db_hz-10*std::log10(aligned->config.sample_rate/2.);
                    channel.snr_db=std::clamp(snr,-300.,300.);
                    const auto& support=curve_entry(cache,options,channel,inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop).estimate;
                    if(snr>=-300&&snr<=300&&search_supported(support)&&support.receiver_workspace_supported) {
                        output.erase(target);cpu_output.erase(target);
                    }
                }
            }
        }
        return added;
    };
    // The selected one-bit probability comes from the already evaluated
    // headline, including when that headline describes a longer draft.
    output.emplace(inputs.target_db_hz,receive_point(inputs.target_db_hz,selected_one,
        model.one_bit_confidence_available||model.one_bit_reference_available));
    cpu_output.emplace(inputs.target_db_hz,cpu_point(inputs.target_db_hz,selected_one));
    // Check every existing graph sample cheaply. Unsupported geometry breaks
    // the overlay; short/coherent profiles can be evaluated analytically here.
    for(const auto& point:model.points)probe(point.target_db_hz,false);
    // Integration is chosen from target C/N0. Its receive transition is
    // usually near the actual link C/N0; probe both sides before refining.
    const std::array coarse{model.actual_cn0_db_hz,model.actual_cn0_db_hz-6,
        model.actual_cn0_db_hz+6,high,model.clock_target.value_or(low),
        model.actual_cn0_db_hz-3,model.actual_cn0_db_hz-12,low};
    for(const auto target:coarse)probe(std::clamp(target,low,high),expensive<coarse_limit);
    // Refine the largest probability change first. Also inspect long gaps
    // between supported samples: a phase-limited peak can sit between equal
    // endpoint values. Genuine unsupported intervals remain explicit.
    for(unsigned iteration=0;iteration<refinement_limit&&expensive<expensive_limit;++iteration) {
        double best=-1,middle=0;
        auto previous=output.end();
        for(auto next=output.begin();next!=output.end();++next) {
            if(!next->second.confidence_available&&!next->second.reference_available)continue;
            if(previous==output.end()){previous=next;continue;}
            const auto begin=previous;previous=next;
            const auto width=next->first-begin->first;
            if(width<.25)continue;
            const auto difference=std::abs(next->second.success_probability-begin->second.success_probability);
            const auto priority=difference>.05?1+difference*std::min(width,12.):width/100;
            if(priority>best){best=priority;middle=(begin->first+next->first)/2;}
        }
        if(best<0||!probe(middle,true))break;
    }
    model.receive_points.reserve(output.size());
    for(const auto& [target,point]:output){(void)target;model.receive_points.push_back(point);}
    model.cpu_points.reserve(cpu_output.size());
    for(const auto& [target,point]:cpu_output){(void)target;model.cpu_points.push_back(point);}
    // The ordinary GUI builds its own conditional source-requirements forecast
    // before any tone is present. Explicit measured scenarios may override an
    // exact target, without changing baseline curves/cache or payload inputs.
    if(!inputs.receiver_settings||inputs.receiver_settings->helper.power_w<=0)return;
    // At most12 temporary native constructors per complete background plan.
    // Unchecked intermediate targets are not additional qualification evidence.
    std::set<double> work_targets{inputs.target_db_hz};
    const auto stride=std::max<std::size_t>(1,(cpu_output.size()+9)/10);
    std::size_t work_index=0;
    for(const auto& [target,point]:cpu_output) {
        (void)point;if(work_index++%stride==0)work_targets.insert(target);
    }
    if(!cpu_output.empty())work_targets.insert(cpu_output.rbegin()->first);
    for(const auto& [target,ordinary_point]:cpu_output) {
        (void)ordinary_point;estimate_detail::check(stop);
        const auto geometry=try_resolve(inputs,target);
        if(!geometry) {
            HelperCpuPoint point;point.target_db_hz=target;point.reason="Ordinary target geometry unavailable";
            HelperWorkPoint work;work.target_db_hz=target;work.native.reason=point.reason;
            model.helper_work_points.push_back(std::move(work));
            model.helper_receive_points.push_back(ReceivePoint{target});
            HelperBackendFeasibilityPoint gap;gap.target_db_hz=target;
            model.helper_backend_feasibility_points.push_back(gap);
            model.helper_cpu_points.push_back(std::move(point));continue;
        }
        auto options=inputs.options;options.modem=*geometry;
        auto channel=inputs.channel;
        const auto sample_snr=model.actual_cn0_db_hz-10*std::log10(geometry->sample_rate/2.);
        channel.snr_db=std::clamp(sample_snr,-300.,300.);
        HelperCpuPoint point;point.target_db_hz=target;
        if(target==inputs.target_db_hz&&!inputs.helper_joint_diagnostic_reason.empty())
            point.reason=inputs.helper_joint_diagnostic_reason;
        std::optional<simulation::QualifiedHelperFrequencyScenario> scenario;
        const auto supplied=std::find_if(inputs.helper_frequency_scenarios.begin(),inputs.helper_frequency_scenarios.end(),
            [&](const auto& item){return item.target_db_hz==target;});
        if(supplied!=inputs.helper_frequency_scenarios.end())scenario=supplied->scenario;
        else if(inputs.helper_executed_joint_clock&&target==inputs.target_db_hz) {
            const auto measured=simulation::helper_executed_joint_scenario(one_bit_estimate(options),options,channel,
                inputs.helper_executed_joint_clock,inputs.helper_forecast_costs,inputs.receiver_work_mode,
                inputs.receiver_timing_model,receiver_settings(inputs),stop);
            scenario=measured.scenario;point.reason=measured.reason;
            if(scenario)point.executed_joint_geometry=measured.metadata;
        }
        if(!scenario) {
        const auto measured_decline=point.reason;
        if(!inputs.helper_forecast_plan_sampled_capture&&
                (!inputs.helper_forecast_capture_sample_rate||!inputs.helper_forecast_capture_passband_hz))
            point.reason="Conditional raw-frequency forecast needs the actual or explicitly planned physical capture rate/passband; logical modem Fs is not that format";
        else if(!inputs.helper_forecast_plan_sampled_capture&&inputs.helper_forecast_capture_logical_sample_rate&&
                *inputs.helper_forecast_capture_logical_sample_rate!=geometry->sample_rate)
            point.reason="Current capture transport does not certify this target's different logical sample rate; reacquire its actual format before forecasting";
        else try {
            auto settings=*inputs.receiver_settings;settings.transfer=options;
            settings.helper.data_power_w=std::pow(10.,(inputs.tx_dbm-30)/10);
            if(options.key)settings.helper.callsign.clear();
            const auto gain=modem::outer_dsss_transmit_gain(*geometry);
            const auto placement=helper::placement_band(*geometry,live::detail::helper_occupied_bandwidth(settings));
            const auto helper_plan=helper::plan(settings.helper,placement,120000000,0,gain);
            const auto power=std::min(settings.helper.power_w,settings.helper.data_power_w*std::pow(10.,settings.helper.ratio_limit_db/10));
            auto cn0=30+10*std::log10(power)-inputs.path_loss_db-inputs.noise_density_dbm_hz;
            if(inputs.receiver_work_mode==simulation::ReceiverWorkMode::sampled_simulation)cn0+=20*std::log10(gain);
            simulation::HelperFrequencyForecastRequest request;
            request.physical_passband_lower_hz=0;
            if(inputs.helper_forecast_plan_sampled_capture) {
                const auto capture=live::detail::shared_helper_capture(*geometry,helper_plan.frequency_hz);
                request.physical_sample_rate=modem::SampledSimulationChannel::planned_physical_sample_rate(
                    *geometry,settings.helper,live::detail::helper_occupied_bandwidth(settings),capture);
                request.physical_passband_upper_hz=std::nextafter(request.physical_sample_rate/2.,0.);
                request.fixed_nominal_rate_conversion=true; // Actual pure halfband-clock selection; no analog radio model.
            } else {
                request.physical_sample_rate=*inputs.helper_forecast_capture_sample_rate;
                request.physical_passband_upper_hz=*inputs.helper_forecast_capture_passband_hz;
                request.fixed_nominal_rate_conversion=inputs.helper_forecast_fixed_nominal_rate_conversion;
            }
            request.helper_frequency_hz=helper_plan.frequency_hz;request.expected_usable_helper_cn0_db_hz=cn0;
            request.assume_independently_qualified_punctuation=inputs.helper_forecast_assume_qualified_punctuation;
            request.assume_continuous_helper_phase=inputs.receiver_work_mode==simulation::ReceiverWorkMode::sampled_simulation||
                !geometry->fake_hopping;
            request.validated_coverage=inputs.helper_forecast_validation;
            const auto forecast=simulation::helper_frequency_forecast(one_bit_estimate(options),options,channel,request,
                inputs.helper_forecast_costs,inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop);
            point.requirements=forecast.requirements;point.masked_requirements=forecast.masked_requirements;
            if(!point.reason.empty())point.reason+=" ";point.reason+=forecast.reason;scenario=forecast.scenario;
        }catch(const Error& error){point.reason=error.what();}
        if(!measured_decline.empty()&&point.reason.find(measured_decline)==std::string::npos)
            point.reason=measured_decline+" "+point.reason;
        }
        if(!inputs.helper_forecast_capture_description.empty()) {
            if(!point.reason.empty())point.reason+=" ";point.reason+=inputs.helper_forecast_capture_description;
        }
        if(!scenario) {
            // Every already-known unsupported target is a graph break, even
            // when it is not one of the at-most12 native metadata probes.
            HelperWorkPoint work;work.target_db_hz=target;work.native.reason=point.reason;
            if(point.requirements.sample_rate)work.expected_helper_cn0_db_hz=point.requirements.expected_cn0_db_hz;
            work.minimum_observation_seconds=point.requirements.minimum_observation_seconds;
            model.helper_work_points.push_back(std::move(work));
            model.helper_receive_points.push_back(ReceivePoint{target});
            HelperBackendFeasibilityPoint gap;gap.target_db_hz=target;
            model.helper_backend_feasibility_points.push_back(gap);
            model.helper_cpu_points.push_back(std::move(point));continue;
        }
        const auto estimate=simulation::estimate_helper_frequency(one_bit_estimate(options),options,true,channel,
            *scenario,{},1,true,100,512,inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop);
        model.helper_receive_points.push_back(receive_point(target,estimate.assisted,sample_snr>=-300&&sample_snr<=300));
        point.scenario_available=estimate.scenario_available;
        point.backend_sustained_available=estimate.scenario_available&&estimate.assisted.receiver_work_supported&&
            estimate.assisted.receiver_workspace_supported&&estimate.assisted.hardware_sustained_modeled&&std::isfinite(estimate.assisted.hardware_sustained_realtime_ratio);
        point.backend_sustained_realtime_ratio=estimate.assisted.hardware_sustained_realtime_ratio;
        point.discovery_available=estimate.discovery_cost_available;point.sustained_available=estimate.qualified_cost_available;
        point.forecast_available=estimate.forecast_available;point.discovery_cpu_seconds=estimate.discovery_cpu_seconds;
        point.prequalification_broad_cpu_seconds=estimate.prequalification_broad_cpu_seconds;
        point.qualified_finite_receiver_cpu_seconds=estimate.assisted.receiver_cpu_seconds;
        point.qualified_sustained_realtime_ratio=estimate.qualified_sustained_realtime_ratio;
        point.ordinary_tracking_realtime_ratio=estimate.ordinary_tracking_realtime_ratio;
        point.additional_frontend_realtime_ratio=estimate.additional_frontend_realtime_ratio;
        point.optional_probe_realtime_ratio=estimate.optional_probe_realtime_ratio;
        if(!point.reason.empty())point.reason+=" ";point.reason+=estimate.reason;
        // A rejected capture/domain/coverage scenario cannot become an
        // "after qualified reference" work point. Missing cost/probability
        // calibration alone leaves scenario_available true and is not a break.
        if(!estimate.scenario_available||work_targets.contains(target)) {
            HelperWorkPoint work;work.target_db_hz=target;
            work.measured_reference=scenario->origin==simulation::HelperScenarioOrigin::measured_reference;
            work.expected_helper_cn0_db_hz=scenario->expected_usable_helper_cn0_db_hz.value_or(
                std::numeric_limits<double>::quiet_NaN());
            work.minimum_observation_seconds=scenario->qualification_aperture_seconds;
            if(estimate.scenario_available) {
                const bool joint=std::any_of(scenario->profiles.begin(),scenario->profiles.end(),[](const auto& profile) {
                    return profile.owner==simulation::HelperExecutionOwner::executed_joint_masked_owner;
                });
                if(joint) {
                    work.native.reason="Actual executed joint subset is priced by the backend curve. The isolated first-bit/work-descriptor component retains all LF lanes and does not accept this separately qualified correlated subset.";
                    work.native.first_bit.reason=work.native.reason;
                } else {
                work.native_probe_attempted=true;
                simulation::HelperFirstBitConditions finite;
                finite.channel=channel;finite.independent_reference_before_observation=true;
                const bool numerical_range=sample_snr>=-300&&sample_snr<=300;
                work.native=simulation::helper_native_work(options,*scenario,inputs.receiver_work_mode,receiver_settings(inputs),stop,
                    numerical_range?&finite:nullptr);
                if(!numerical_range)work.native.first_bit.reason="Entered payload sample-SNR is outside the ordinary numerical range; the clamped diagnostic channel does not supply a probability";
                if(numerical_range&&work.native.first_bit.available) {
                    // Conditional known-onset first-bit reference only. Never
                    // promote this to draft confidence or physical completion.
                    auto& rx=model.helper_receive_points.back();rx.reference_available=true;
                    rx.probability_reference_only=true;rx.success_probability=work.native.first_bit.correct;
                    rx.probability_trials=work.native.first_bit.trials;
                }
                }
            } else work.native.reason=point.reason;
            model.helper_work_points.push_back(std::move(work));
        }
        const auto ordinary_rx=output.find(target);
        model.helper_backend_feasibility_points.push_back(helper_backend_feasibility(
            ordinary_rx!=output.end()?ordinary_rx->second:ReceivePoint{target},point,geometry->receiver_cpu_budget));
        model.helper_cpu_points.push_back(std::move(point));
    }
}
}

HelperBackendFeasibilityPoint helper_backend_feasibility(const ReceivePoint& ordinary,
        const HelperCpuPoint& conditional,double budget) {
    HelperBackendFeasibilityPoint out;out.target_db_hz=ordinary.target_db_hz;
    out.ordinary_success_probability=ordinary.success_probability;out.cpu_budget=budget;
    out.ordinary_phy_available=(ordinary.confidence_available||ordinary.reference_available)&&
        ordinary.clock_supported&&ordinary.workspace_supported&&
        std::isfinite(ordinary.success_probability)&&ordinary.success_probability>=0&&ordinary.success_probability<=1;
    out.backend_realtime_ratio=conditional.backend_sustained_realtime_ratio;
    out.backend_feasibility_known=conditional.target_db_hz==ordinary.target_db_hz&&conditional.scenario_available&&
        conditional.backend_sustained_available&&std::isfinite(out.backend_realtime_ratio)&&
        out.backend_realtime_ratio>=0&&std::isfinite(budget)&&budget>0;
    out.backend_fits_budget=out.backend_feasibility_known&&out.backend_realtime_ratio<=budget;
    return out;
}

struct Cache::Impl {
    execution::Mutex mutex;
    CurveCache curves;
    std::optional<SupportContext> support_context;
    SupportCache support;
};
Cache::Cache():impl_(std::make_unique<Impl>()) {}
Cache::~Cache()=default;
Cache::Statistics Cache::statistics() const {
    std::lock_guard lock(impl_->mutex);
    return {impl_->curves.entries.size(),impl_->curves.probability_evaluations,
        impl_->curves.probability_reuses,impl_->support.entries.size(),
        impl_->support.evaluations,impl_->support.reuses};
}

std::optional<double> nearest_fit_target(const Inputs& inputs,
        std::span<const double> companion_targets,std::optional<ReceiveBanks> banks) {
    try {
        if(!std::isfinite(inputs.target_db_hz)||inputs.target_db_hz< -200||inputs.target_db_hz>200||
           !std::isfinite(inputs.tx_dbm)||!std::isfinite(inputs.path_loss_db)||inputs.path_loss_db<0||
           !std::isfinite(inputs.noise_density_dbm_hz)||
           companion_targets.size()>=tuning::maximum_receive_targets)return std::nullopt;
        // Match the live limit of 128 receive keys plus a selected key. Empty
        // explicit bank sets must not fall back to an implicit receiver.
        if(banks&&((!banks->plaintext&&!banks->private_keys)||banks->private_keys>129))return std::nullopt;
        modem::validate(inputs.options.modem);
        // Search independently of the requested duration: a target can exceed
        // the sample counter while a nearby practical fallback remains usable.
        const auto maximum_seconds=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())/
            (4.L*inputs.options.modem.sample_rate);
        const auto minimum=std::max(-200.,tuning::pattern_target_symbol_snr_db-
            10*std::log10(static_cast<double>(maximum_seconds)));
        return ClockSearch(inputs,minimum,200,companion_targets,banks).nearest();
    } catch(const Error&) {return std::nullopt;}
}

Model build(const Inputs& inputs,std::stop_token stop) {
    thread_local Cache cache;
    return build(inputs,cache,stop);
}
TargetSteps preview_steps(const Inputs& inputs) {
    TargetSteps result;
    try {
        if(!automatic(inputs.mode)||!std::isfinite(inputs.target_db_hz)||
           inputs.target_db_hz< -200||inputs.target_db_hz>200||!inputs.wire_bits||
           !std::isfinite(inputs.tx_dbm)||!std::isfinite(inputs.path_loss_db)||inputs.path_loss_db<0||
           !std::isfinite(inputs.noise_density_dbm_hz))return result;
        const auto received=inputs.tx_dbm-inputs.path_loss_db;
        const auto cn0=received-inputs.noise_density_dbm_hz;
        if(!std::isfinite(received)||!std::isfinite(cn0)||!std::isfinite(cn0-inputs.target_db_hz))return result;
        auto options=inputs.options;options.modem=resolve(inputs,inputs.target_db_hz);
        options.automatic_receive_profiles=true;
        options.receive_targets_db_hz={inputs.target_db_hz};options.receive_pattern_mode=inputs.mode;
        // The support-only estimator omits transfer validation. Retain the
        // full planner's options and sample-counter guards before advertising
        // any action; one raw bit needs neither PCM nor a payload allocation.
        constexpr std::array<std::uint8_t,1> one_bit{0};
        const auto transmission=transfer::estimate_binary(one_bit,options);
        const auto symbol=modem::symbol_sample_count(options.modem);
        if(!transmission.waveform_samples||transmission.waveform_samples<symbol)return result;
        if(options.modem.symbol_burst.enabled()||modem::scheduled_bursts(options.modem)) {
            const auto seeded=modem::scheduled_estimate_config(transfer::seeded_config(options,options.timestamp));
            (void)add_samples(modem::pattern_transmission_sample_count(seeded,inputs.wire_bits),
                modem::pattern_absence_capture_samples(seeded));
        } else {
            const auto overhead=static_cast<std::uint64_t>(transmission.waveform_samples)-symbol-
                modem::scheduled_symbol_offset(options.modem,options.timestamp,options.modem.stream_phase_samples,0);
            if(inputs.wire_bits>(std::numeric_limits<std::uint64_t>::max()-overhead)/symbol)return result;
            (void)add_samples(add_samples(add_samples(modem::scheduled_symbol_offset(options.modem,options.timestamp,
                options.modem.stream_phase_samples,inputs.wire_bits-1),symbol),overhead),
                modem::pattern_absence_capture_samples(options.modem));
        }
        const auto maximum_seconds=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())/
            (4.L*options.modem.sample_rate);
        const auto minimum=std::max(-200.,tuning::pattern_target_symbol_snr_db-
            10*std::log10(static_cast<double>(maximum_seconds)));
        for(const bool stronger:{true,false}) {
            const auto target=std::clamp(inputs.target_db_hz+(stronger?1:-1),minimum,200.);
            if(stronger?target<=inputs.target_db_hz+1e-8:target>=inputs.target_db_hz-1e-8)continue;
            const auto config=try_resolve(inputs,target);
            if(config&&receiver_support(inputs,*config).fits())
                (stronger?result.stronger:result.weaker)=target;
        }
    } catch(const Error&) {return {};}
    return result;
}
Model build(const Inputs& inputs,Cache& cache,std::stop_token stop,const std::function<void(const Model&)>& selected,
            std::optional<TargetSteps> supplied_steps) {
    std::unique_lock lock(cache.impl_->mutex,std::defer_lock);
    estimate_detail::lock(lock,stop);
    Model result;result.inputs=inputs;result.automatic_mode=automatic(inputs.mode);
    try {
        if(!std::isfinite(inputs.target_db_hz) || inputs.target_db_hz< -200 || inputs.target_db_hz>200 ||
           !std::isfinite(inputs.tx_dbm) || !std::isfinite(inputs.path_loss_db) || inputs.path_loss_db<0 ||
           !std::isfinite(inputs.noise_density_dbm_hz) || !inputs.wire_bits)
            throw Error("Enter a finite signal target, power and noise level, nonnegative path loss, and at least one bit.");
        auto options=inputs.options;options.modem=resolve(inputs,inputs.target_db_hz);
        options.automatic_receive_profiles=true;
        options.receive_targets_db_hz={inputs.target_db_hz};
        options.receive_pattern_mode=inputs.mode;
        const auto& config=options.modem;
        result.received_dbm=inputs.tx_dbm-inputs.path_loss_db;
        result.actual_cn0_db_hz=result.received_dbm-inputs.noise_density_dbm_hz;
        result.margin_db=result.actual_cn0_db_hz-inputs.target_db_hz;
        if(!std::isfinite(result.received_dbm) || !std::isfinite(result.actual_cn0_db_hz) || !std::isfinite(result.margin_db))
            throw Error("The link budget exceeds the numerical range.");
        const auto symbol=modem::symbol_sample_count(config);
        result.bit_seconds=seconds(config);

        // Probe one exact raw bit, retaining only bounded transfer scratch.
        // Scale its payload sample count without allocating a draft or PCM.
        constexpr std::array<std::uint8_t,1> one_bit{0};
        auto transmission=transfer::estimate_binary(one_bit,options);
        const auto single_transmission=transmission;
        if(!transmission.waveform_samples || transmission.waveform_samples<symbol)
            throw Error("planned duration exceeds the platform sample counter");
        std::uint64_t payload=0,waveform=0;
        if(config.symbol_burst.enabled()||modem::scheduled_bursts(config)) {
            // Nulls depend on the absolute keyed frame, so scaling a one-bit
            // wall duration would invent airtime and can underflow overhead.
            const auto original=transfer::seeded_config(options,options.timestamp);
            const auto seeded=modem::scheduled_estimate_config(original);
            result.scheduled_duration_representative=seeded.stream_epoch!=original.stream_epoch||
                seeded.stream_phase_samples!=original.stream_phase_samples;
            payload=scheduled_content_samples(seeded,inputs.wire_bits);
            waveform=modem::pattern_transmission_sample_count(seeded,inputs.wire_bits);
        } else {
            const auto overhead=static_cast<std::uint64_t>(transmission.waveform_samples)-symbol-
                modem::scheduled_symbol_offset(options.modem,options.timestamp,options.modem.stream_phase_samples,0);
            if(inputs.wire_bits>(std::numeric_limits<std::uint64_t>::max()-overhead)/symbol)
                throw Error("planned duration exceeds the modem sample counter");
            payload=add_samples(modem::scheduled_symbol_offset(config,options.timestamp,config.stream_phase_samples,inputs.wire_bits-1),symbol);
            waveform=add_samples(payload,overhead);
        }
        const auto finish=add_samples(waveform,modem::pattern_absence_capture_samples(config));
        result.send_seconds=static_cast<double>(waveform)/config.sample_rate;
        result.finish_seconds=static_cast<double>(finish)/config.sample_rate;
        transmission.wire_bits=inputs.wire_bits;
        transmission.coded_bytes=transmission.content_bytes=inputs.wire_bits/8+(inputs.wire_bits%8!=0);
        transmission.content_seconds=transmission.coded_seconds=static_cast<double>(payload)/config.sample_rate;
        transmission.total_seconds=result.send_seconds;
        transmission.waveform_samples=waveform<=std::numeric_limits<std::size_t>::max()?
            static_cast<std::size_t>(waveform):0;

        result.shaped_band=modem::pattern_pulse_enabled(config);
        result.occupied_bandwidth_hz=result.shaped_band?
            (1+modem::pattern_pulse_rolloff)*config.sample_rate/static_cast<double>(modem::pattern_chip_samples(config)):
            config.bandwidth_hz;
        result.gate_bandwidth_allowance_hz=modem::waveform_gate_bandwidth_allowance_hz(config);
        result.occupied_bandwidth_hz+=result.gate_bandwidth_allowance_hz;
        result.low_audio_hz=config.carrier_hz-result.occupied_bandwidth_hz/2;
        result.high_audio_hz=config.carrier_hz+result.occupied_bandwidth_hz/2;
        result.observer_hypothetical=!options.key || config.spreading_mode!=modem::SpreadingMode::pattern;
        if(!result.observer_hypothetical) {
            const auto observer=lpi::estimate(transmission,options,inputs.observer_hopping,stop);
            result.observer_available=observer.status==lpi::Status::available;
            result.observer_ratio=observer.equivalent_symbols;
        }

        auto channel=inputs.channel;
        const auto sample_snr=result.actual_cn0_db_hz-10*std::log10(config.sample_rate/2.);
        // The existing simulator accepts only -300..300 dB. Clamping its
        // strength input does not alter search or workspace checks. Keep
        // confidence unavailable outside the estimator's supported range.
        channel.snr_db=std::clamp(sample_snr,-300.,300.);
        const auto receiver=receiver_estimate(transmission,options,true,channel,{},1,true,100,4096,
            inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop);
        result.clock_search_supported=search_supported(receiver);
        result.receiver_workspace_supported=receiver.receiver_workspace_supported;
        result.confidence_available=receiver.confidence_available&&sample_snr>=-300&&sample_snr<=300;
        result.one_bit_confidence_available=receiver.one_bit_confidence_available&&sample_snr>=-300&&sample_snr<=300;
        result.probability_reference_only=receiver.probability_reference_only;
        result.reference_probability_available=receiver.reference_probability_available&&sample_snr>=-300&&sample_snr<=300;
        result.one_bit_reference_available=receiver.one_bit_reference_available&&sample_snr>=-300&&sample_snr<=300;
        result.success_probability=result.confidence_available||result.reference_probability_available?receiver.success_probability:0;
        result.one_bit_success_probability=result.one_bit_confidence_available||result.one_bit_reference_available?receiver.one_bit_success_probability:0;
        result.probability_trials=receiver.probability_trials;
        result.success_probability_low=receiver.success_probability_low;
        result.success_probability_high=receiver.success_probability_high;
        result.one_bit_probability_low=receiver.one_bit_probability_low;
        result.one_bit_probability_high=receiver.one_bit_probability_high;
        result.probability_interval_available=receiver.probability_interval_available;
        result.probability_search_approximation=receiver.probability_search_approximation;
        result.probability_carrier_candidates=receiver.probability_carrier_candidates;
        result.probability_model_limit=receiver.probability_model_limit;
        result.phase_coherence_loss_db=receiver.phase_coherence_loss_db;
        result.coherent_reference_only=receiver.coherent_reference_only;
        result.drift_model_available=receiver.drift_model_available&&(result.confidence_available||result.reference_probability_available);
        result.coherent_success_probability=result.confidence_available||result.reference_probability_available?receiver.coherent_success_probability:0;
        result.section_phase_coherence_loss_db=receiver.section_phase_coherence_loss_db;
        result.differential_windows=receiver.differential_windows;
        result.differential_window_seconds=receiver.differential_window_seconds;
        result.differential_model_available=receiver.differential_model_available;
        result.differential_added_detection_probability=receiver.differential_added_detection_probability;
        auto single_receiver=inputs.wire_bits==1?receiver:
            receiver_estimate(single_transmission,options,true,channel,{},1,false,100,4096,
                inputs.receiver_work_mode,inputs.receiver_timing_model,receiver_settings(inputs),stop);
        // Draft length changes continuation and total work, but acquisition
        // already supplies this one-bit probability without another trial run.
        single_receiver.success_probability=receiver.one_bit_success_probability;
        single_receiver.confidence_available=receiver.one_bit_confidence_available;
        single_receiver.reference_probability_available=receiver.one_bit_reference_available;
        single_receiver.probability_reference_only=receiver.probability_reference_only;
        single_receiver.probability_trials=receiver.probability_trials;
        single_receiver.drift_model_available=receiver.drift_model_available;
        single_receiver.coherent_reference_only=receiver.coherent_reference_only;
        result.one_bit_cpu_seconds=single_receiver.symbol_burst.modeled?
            single_receiver.symbol_burst.finite_receiver_cpu_seconds:single_receiver.cpu_seconds;
        result.receiver_cpu_seconds=single_receiver.symbol_burst.modeled?
            single_receiver.symbol_burst.finite_receiver_cpu_seconds:single_receiver.receiver_cpu_seconds;
        result.kernel_rebuild_upper_bound=single_receiver.kernel_rebuild_upper_bound;
        result.symbol_burst=single_receiver.symbol_burst;
        result.finite_cpu_realtime_ratio=single_receiver.symbol_burst.modeled?single_receiver.symbol_burst.finite_realtime_ratio:single_receiver.simulated_seconds>0?
            single_receiver.receiver_cpu_seconds/single_receiver.simulated_seconds:0;
        result.hardware_sustained_modeled=single_receiver.hardware_sustained_modeled;
        result.hardware_sustained_realtime_ratio=single_receiver.hardware_sustained_realtime_ratio;
        result.hardware_cohort_lifetime_seconds=single_receiver.hardware_cohort_lifetime_seconds;
        result.hardware_sustained_receiver_instances=single_receiver.hardware_sustained_receiver_instances;
        result.cpu_realtime_ratio=planned_cpu_ratio(single_receiver);
        result.cpu_realtime_upper_ratio=single_receiver.symbol_burst.modeled?
            std::max(single_receiver.symbol_burst.finite_realtime_upper_ratio,single_receiver.symbol_burst.tracked_realtime_upper_ratio):
            single_receiver.simulated_seconds>0?
            (single_receiver.receiver_cpu_seconds+std::max(0.,single_receiver.receiver_kernel_rebuild_upper_seconds-
                single_receiver.receiver_kernel_rebuild_seconds))/single_receiver.simulated_seconds:0;
        result.cpu_realtime_upper_ratio=std::max(result.cpu_realtime_upper_ratio,result.cpu_realtime_ratio);
        result.cpu_per_bit_ratio=result.one_bit_cpu_seconds/result.bit_seconds;
        // Unknown execution cost leaves a graph gap. Keep finite presentation
        // placeholders without converting an uncalibrated path to zero work.
        const auto finite_ratio=[](double value) {return std::isfinite(value)&&value>=0?value:0.;};
        result.finite_cpu_realtime_ratio=finite_ratio(result.finite_cpu_realtime_ratio);
        result.cpu_realtime_ratio=finite_ratio(result.cpu_realtime_ratio);
        result.cpu_realtime_upper_ratio=finite_ratio(result.cpu_realtime_upper_ratio);
        result.cpu_per_bit_ratio=finite_ratio(result.cpu_per_bit_ratio);
        result.hardware_sustained_realtime_ratio=finite_ratio(result.hardware_sustained_realtime_ratio);
        result.receiver_geometry=single_receiver.receiver_geometry;
        result.frequency_rate_hypotheses=single_receiver.frequency_rate_hypotheses;
        result.epoch_hypotheses=single_receiver.epoch_hypotheses;
        result.timing_hypotheses=single_receiver.timing_hypotheses;
        result.timing_phase_groups=single_receiver.timing_phase_groups;
        result.timing_window_modeled=single_receiver.timing_window_modeled;
        result.initial_fft_acquisition_batches=single_receiver.fft_acquisition_batches;
        result.initial_fft_retained_acquisition_batches=single_receiver.fft_retained_acquisition_batches;
        result.new_epoch_admissions=single_receiver.new_epoch_admissions;
        result.new_epoch_full_fft_batches=single_receiver.new_epoch_full_fft_batches;
        result.new_epoch_qualified_ready_batch_slots=single_receiver.new_epoch_qualified_ready_batch_slots;
        result.new_epoch_retained_fft_batches=single_receiver.new_epoch_retained_fft_batches;
        result.receiver_work_assumptions=single_receiver.receiver_work_assumptions;
        result.fallback_cpu_realtime_ratio=single_receiver.simulated_seconds>0?
            single_receiver.fallback_receiver_cpu_seconds/single_receiver.simulated_seconds:0;
        if(single_receiver.hardware_sustained_modeled)result.fallback_cpu_realtime_ratio=std::max(
            result.fallback_cpu_realtime_ratio,single_receiver.fallback_hardware_sustained_realtime_ratio);
        result.fallback_cpu_realtime_ratio=finite_ratio(result.fallback_cpu_realtime_ratio);
        result.fallback_timing_hypotheses=single_receiver.fallback_timing_hypotheses;
        result.one_bit_cpu_available=single_receiver.receiver_work_supported&&single_receiver.receiver_workspace_supported&&
            std::isfinite(result.one_bit_cpu_seconds)&&std::isfinite(result.receiver_cpu_seconds)&&
            std::isfinite(result.cpu_realtime_ratio)&&result.one_bit_cpu_seconds>0&&single_receiver.simulated_seconds>0;
        // The selected marker already receives single_receiver directly.
        // Keep its 4096-trial result out of the 512-trial curve cache so a
        // previously selected target cannot change a later curve's precision.
        if(!receiver.carrier_in_search||!receiver.clock_in_search)result.receiver_status="Clock outside RX search";
        if(receiver.oscillator_search_limited) {
            if(!result.receiver_status.empty())result.receiver_status+=" · ";
            result.receiver_status+="Oscillator margin coverage incomplete";
        }
        if(!receiver.receiver_workspace_supported) {
            if(!result.receiver_status.empty())result.receiver_status+=" · ";
            result.receiver_status+="Wide RX search exceeds RAM";
            if(inputs.dsp_workspace_percent)result.receiver_status+=" ("+std::to_string(inputs.dsp_workspace_percent)+"%)";
        }
        if(!receiver.receiver_work_supported) {
            if(!result.receiver_status.empty())result.receiver_status+=" · ";
            result.receiver_status+="CPU estimate unavailable for selected receiver";
        }
        if(result.receiver_status.empty())result.receiver_status="Clock and RAM fit · reception unverified";
        helper_reference_plan(result,config);

        if(selected) {
            estimate_detail::check(stop);
            auto current=result;current.available=true;current.curves_calculating=true;
            const auto steps=supplied_steps?*supplied_steps:preview_steps(inputs);
            current.stronger_fit_target=steps.stronger;current.weaker_fit_target=steps.weaker;
            selected(current);
        }
        // Leave sample-counter headroom for one symbol of physical absence
        // and the fixed waveform overhead even at the graph's weakest end.
        const auto maximum_seconds=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())/
            (4.L*config.sample_rate);
        const auto minimum_target=std::max(-200.,tuning::pattern_target_symbol_snr_db-
            10*std::log10(static_cast<double>(maximum_seconds)));
        constexpr double maximum_target=200;
        if(result.automatic_mode) {
            result.fast_target=duration_boundary(inputs,minimum_target,maximum_target,1,stop);
            result.day_target=duration_boundary(inputs,minimum_target,maximum_target,seconds_per_day,stop);
            const auto context=support_context(inputs);
            if(cache.impl_->support_context!=context) {
                cache.impl_->support.entries.clear();cache.impl_->support_context=context;
            }
            const auto choices=ClockSearch(inputs,minimum_target,maximum_target,{},std::nullopt,
                &cache.impl_->support,stop).run();
            if(choices.boundary) {
                result.clock_target=choices.boundary->target;result.clock_limit_reason=choices.boundary->reason;
            }
            result.stronger_fit_target=choices.stronger;
            result.weaker_fit_target=choices.weaker;
        }
        auto low=std::min(-35.,inputs.target_db_hz-3),high=std::max(25.,inputs.target_db_hz+3);
        for(const auto boundary:{result.fast_target,result.day_target,result.clock_target})if(boundary) {
            low=std::min(low,*boundary-3);high=std::max(high,*boundary+3);
        }
        low=std::max(minimum_target,low);high=std::min(maximum_target,high);
        std::vector<double> targets;targets.reserve(curve_samples+4);
        for(std::size_t i=0;i<curve_samples;++i)
            targets.push_back(low+(high-low)*static_cast<double>(i)/static_cast<double>(curve_samples-1));
        targets.push_back(inputs.target_db_hz);
        for(const auto boundary:{result.fast_target,result.day_target,result.clock_target})if(boundary)targets.push_back(*boundary);
        std::sort(targets.begin(),targets.end());
        targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
        result.points.reserve(targets.size());
        for(const auto target:targets) {
            estimate_detail::check(stop);
            const auto geometry=try_resolve(inputs,target);
            if(!geometry)continue;
            auto point_options=inputs.options;point_options.modem=*geometry;
            Point point;point.target_db_hz=target;point.bit_seconds=seconds(*geometry);
            transfer::Estimate one;one.wire_bits=1;one.total_seconds=point.bit_seconds;
            if(!result.observer_hypothetical) {
                const auto estimate=lpi::estimate(one,point_options,inputs.observer_hopping,stop);
                point.observer_available=estimate.status==lpi::Status::available;
                point.observer_ratio=estimate.equivalent_symbols;
            }
            result.points.push_back(point);
        }
        receive_curve(result,single_receiver,cache.impl_->curves,stop);
        estimate_detail::check(stop);
        result.available=true;
    } catch(const Error& error) {
        result.error=error.what();result.receiver_status="Plan unavailable";
    }
    return result;
}

std::string duration(double value) {
    if(!std::isfinite(value) || value<0)return "Unavailable";
    if(value==0)return "0 sec";
    if(value<1)return number(value*1000)+" ms";
    if(value<60)return number(value)+" sec";
    if(value<3600) {
        const auto rounded=static_cast<unsigned>(std::round(value));
        const auto remainder=rounded%60;
        return std::to_string(rounded/60)+" min"+(remainder?" "+std::to_string(remainder)+" sec":"");
    }
    if(value<seconds_per_day) {
        const auto minutes=static_cast<unsigned>(std::round(value/60));
        return std::to_string(minutes/60)+" hr"+(minutes%60?" "+std::to_string(minutes%60)+" min":"");
    }
    if(value<365.25*seconds_per_day) {
        const auto hours=static_cast<unsigned>(std::round(value/3600));
        const auto days=hours/24;
        return std::to_string(days)+(days==1?" day":" days")+(hours%24?" "+std::to_string(hours%24)+" hr":"");
    }
    return number(value/(365.25*seconds_per_day))+" yr";
}
}
