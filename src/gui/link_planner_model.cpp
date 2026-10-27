#include "../scheduled_acquisition_policy.hpp"
#include "../live_helper.hpp"
#include "../live_helper_prior.hpp"
#include "../live_helper_frontend.hpp"
#include "../helper_reference_search.hpp"
#include "datapump/helper_reference_precision.hpp"
#include "../receiver_oscillator_policy.hpp"
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
#include <numbers>
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
    settings.transfer=model.inputs.options;
    settings.simulation=model.inputs.receiver_work_mode==simulation::ReceiverWorkMode::sampled_simulation;
    if(settings.simulation) {
        settings.simulation_phase_noise_degrees_per_sqrt_second=model.inputs.channel.phase_noise_degrees_per_sqrt_second;
        settings.simulation_clock_error_ppm=model.inputs.channel.clock_error_ppm;
    }
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
        result.tone_snr_db=result.cn0_db_hz-10; // Legacy presentation cell, not a lock gate.
        result.pilot_power_w=result.effective_power_w*helper::pilot_power_fraction;
        result.pilot_cn0_db_hz=result.cn0_db_hz+10*std::log10(helper::pilot_power_fraction);
        result.pilot_tone_snr_db=result.pilot_cn0_db_hz-10;
        result.auxiliary_cn0_db_hz=result.cn0_db_hz+10*std::log10(helper::auxiliary_power_fraction);
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
            result.simulation_pilot_cn0_db_hz=result.pilot_cn0_db_hz+result.simulation_level_db;
            result.simulation_pilot_tone_snr_db=result.pilot_tone_snr_db+result.simulation_level_db;
        }
        result.tone_level_sufficient=(result.simulation_level_modeled?
            result.simulation_pilot_tone_snr_db:result.pilot_tone_snr_db)>=6;
        // This is the same placement as Live, without claiming that an unknown
        // hardware output rate or radio filter can pass the resulting tone.
        const auto placement=helper::placement_band(config,live::detail::helper_occupied_bandwidth(settings));
        const auto plan=helper::plan(settings.helper,placement,120000000,0,waveform_gain);
        result.frequency_hz=plan.frequency_hz;result.auxiliary_frequency_hz=plan.auxiliary_frequency_hz;
        result.geometry_available=true;
        result.status="Manual real-time helper reference: Robust acquisition waits for helper lock. Disable Helper Carrier for ordinary oscillator search. Conditional receiver cost preserves the full residual LF domain; helper tracking/conversion cost and lock probability are not calibrated.";
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
// The tone forecast depends on its physical request, not on the payload's
// target SNR. Neighboring curve points commonly repeat the entire request.
// Reuse that bounded numeric forecast under Cache's existing mutex; never
// cache runtime qualification or a receiver-cost result here.
struct HelperPrecisionCache {
    using Key=std::tuple<std::uint32_t,double,double,std::size_t,bool,double,double,double,double,double,bool,double,double>;
    struct Entry {Key key;helper::TonePrecisionPrediction prediction;};
    std::array<std::optional<Entry>,16> entries;
    std::size_t next=0;
    helper::TonePrecisionPrediction predict(const helper::TonePrecisionRequest& r) {
        const Key key{r.sample_rate,r.straight_cn0_db_hz,r.phase_noise_radians_per_sqrt_second,
            r.metadata_symbols,r.punctuate,r.baseline_lead_seconds,r.target_half_width_hz,
            r.max_extra_seconds,r.maximum_tracking_slew_hz_per_second,r.maximum_source_capture_ratio,
            r.pilot_segmented,r.pilot_coherent_aperture_seconds,r.pilot_estimator_efficiency};
        for(const auto& entry:entries)if(entry && entry->key==key)return entry->prediction;
        auto prediction=helper::predict_tone_precision(r);
        entries[next]=Entry{key,prediction};next=(next+1)%entries.size();
        return prediction;
    }
};
struct CurveCache {
    std::vector<CurveEntry> entries;
    std::size_t probability_evaluations=0,probability_reuses=0;
    HelperPrecisionCache helper_precision;
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
RealtimeHelperCpuPoint realtime_helper_point(const Inputs& inputs,const modem::Config& config,
        double target,HelperPrecisionCache& precision_cache,std::stop_token stop) {
    RealtimeHelperCpuPoint out;out.target_db_hz=target;
    if(!inputs.receiver_settings || !(inputs.receiver_settings->helper.power_w>0))return out;
    try {
        estimate_detail::check(stop);
        if(!config.oscillator_search)throw Error("Conditional helper search requires an oscillator model");
        if(config.symbol_burst.enabled() || modem::scheduled_bursts(config))
            throw Error("Conditional helper cost is not modeled for Symbol_Burst or scheduled bursts");
        auto settings=*inputs.receiver_settings;settings.transfer=inputs.options;settings.transfer.modem=config;
        settings.simulation=inputs.receiver_work_mode==simulation::ReceiverWorkMode::sampled_simulation;
        if(settings.simulation) {
            settings.simulation_phase_noise_degrees_per_sqrt_second=inputs.channel.phase_noise_degrees_per_sqrt_second;
            settings.simulation_clock_error_ppm=inputs.channel.clock_error_ppm;
        }
        // The duplicate shared baseline forecast must also see the editable
        // link budget, rather than a retained Live settings snapshot.
        settings.simulation_snr_db=inputs.tx_dbm-inputs.path_loss_db-inputs.noise_density_dbm_hz-
            10*std::log10(config.sample_rate/2.);
        settings.helper.data_power_w=std::pow(10.,(inputs.tx_dbm-30)/10);
        if(settings.transfer.key)settings.helper.callsign.clear();
        if(settings.helper.frequency==helper::Frequency::cochannel)
            throw Error("Cochannel helper capture is not modeled by the corrected-input cost estimate");
        const auto gain=modem::outer_dsss_transmit_gain(config);
        const auto width=live::detail::helper_occupied_bandwidth(settings);
        const auto plan=helper::plan(settings.helper,helper::placement_band(config,width),120000000,0,gain);
        std::uint32_t physical=0;
        if(inputs.helper_forecast_plan_sampled_capture)physical=modem::SampledSimulationChannel::planned_physical_sample_rate(
            config,settings.helper,width,live::detail::shared_helper_capture(config,plan.frequency_hz));
        else if(inputs.helper_forecast_capture_sample_rate && inputs.helper_forecast_fixed_nominal_rate_conversion &&
                inputs.helper_forecast_capture_logical_sample_rate==config.sample_rate)
            physical=*inputs.helper_forecast_capture_sample_rate;
        else throw Error("Raw capture format/fixed nominal conversion is unknown; no helper cost forecast");
        out.physical_sample_rate=physical;
        out.minimum_seed_seconds=static_cast<double>(helper::forecast_detail::pilot_discovery_samples(physical))/physical;
        if(plan.frequency_hz<200 || plan.frequency_hz>physical/2.-200)
            throw Error("Nominal helper is outside the real-time tracker's DC/Nyquist noise-projection margin");
        const auto power=std::min(settings.helper.power_w,settings.helper.data_power_w*std::pow(10.,settings.helper.ratio_limit_db/10));
        auto cno=30+10*std::log10(power)-inputs.path_loss_db-inputs.noise_density_dbm_hz;
        if(inputs.receiver_work_mode==simulation::ReceiverWorkMode::sampled_simulation)cno+=20*std::log10(gain);
        cno+=10*std::log10(helper::pilot_power_fraction);
        // Use the shared production request: actual metadata/punctuation and
        // oscillator process, not the retired uninterrupted 384-date ideal.
        const auto receive_bound=live::detail::helper_receive_envelope_bound(settings);
        auto requirements=live::detail::helper_envelope_baseline_requirements(settings,config,receive_bound);
        requirements.frequency_precision_extension_seconds=0;
        const auto baseline=helper::plan_envelope(config,requirements);
        auto request=live::detail::helper_tone_precision_request(settings,config,physical,cno,
            static_cast<double>(baseline.lead_seconds),requirements.maximum_source_capture_ratio);
        request.pilot_segmented=true;
        auto& reference=out.precision_reference;
        reference.cn0_db_hz=request.straight_cn0_db_hz;
        reference.phase_noise_radians_per_sqrt_second=request.phase_noise_radians_per_sqrt_second;
        reference.maximum_tracking_slew_hz_per_second=request.maximum_tracking_slew_hz_per_second;
        reference.maximum_source_capture_ratio=request.maximum_source_capture_ratio;
        reference.coherent_aperture_seconds=request.pilot_coherent_aperture_seconds/request.maximum_source_capture_ratio;
        reference.estimator_efficiency=request.pilot_estimator_efficiency;
        reference.ideal_one_second_half_width_hz=helper_ideal_tone_half_width(reference.cn0_db_hz,1);
        out.precision=precision_cache.predict(request);
        // Match transmit airtime even when the bounded lead is selected without
        // a numeric precision estimate. Such a cap never creates an RF search.
        requirements.frequency_precision_extension_seconds=
            (out.precision.supported || out.precision.extension_capped)?out.precision.extra_lead_seconds:0;
        const auto envelope=helper::plan_envelope(config,requirements);
        out.planned_lead_seconds=static_cast<double>(envelope.lead_seconds);
        out.planned_tail_seconds=static_cast<double>(envelope.tail_seconds);
        // This is an ideal uninterrupted observation over the same physical
        // elapsed lead, not a claim that numeric segment history retains phase.
        reference.ideal_lead_observation_seconds=out.planned_lead_seconds/request.maximum_source_capture_ratio;
        reference.ideal_lead_half_width_hz=helper_ideal_tone_half_width(reference.cn0_db_hz,
            reference.ideal_lead_observation_seconds);
        if(out.precision.supported)
            reference.segmented_lead_half_width_hz=out.precision.segmented_noise_reference_half_width_hz;
        if(!out.precision.supported)throw Error("Automatic helper precision lead prediction declined: "+out.precision.reason);
        out.observation_seconds=out.precision.usable_tone_seconds;
        const auto price_reference=[&](double uncertainty,const std::string& precision_reason) {
            RealtimeHelperReceiverWork out;out.uncertainty_hz=uncertainty;
            try {
                auto options=inputs.options;options.modem=config;
                const auto correction=transfer::receiver_utc_rate_correction(config,options);
                // Match HelperInputFrontend::admit_realtime: when the nominal payload
                // guard reaches DC, its immutable input basis is raw mono PCM (q=0).
                // The measured helper offset M then centers the ordinary templates;
                // DATA retains the residual (D-H)*clock term, not a fictitious NCO.
                const auto reference_guard=std::max(32.,4*out.uncertainty_hz);
                const bool folded_reference=config.carrier_hz-helper::occupied_bandwidth(config)/2-reference_guard<=10;
                const auto reference_center=folded_reference?
                    inputs.channel.frequency_offset_hz+plan.frequency_hz*inputs.channel.clock_error_ppm*1e-6:0.;
                const auto search=modem::helper_reference_search(config,out.uncertainty_hz,plan.frequency_hz,
                    reference_center,correction,modem::HelperReferenceClockRelation::common_corrected_source,{},
                    modem::PatternUtcClockGrid::complete_expanded_domain);
                out.rf_half_width_hz=search.rf_half_width_hz;
                out.clock_half_width_ppm=search.catalogue.requested_clock_half_width_ppm;
                out.frequency_rate_hypotheses=search.catalogue.hypotheses.size();
                std::set<double> clocks;
                for(const auto& pair:search.catalogue.hypotheses)clocks.insert(pair.clock_error_ppm);
                out.retained_clock_rates=clocks.size();
                // This is a conditional forecast at the configured channel offset,
                // never qualification inferred from expected power. Match the runtime
                // shared-radio/no-UTC formula and ORIGINAL-cell intersection exactly.
                const auto observed_helper=plan.frequency_hz+inputs.channel.frequency_offset_hz+
                    plan.frequency_hz*inputs.channel.clock_error_ppm*1e-6;
                out.shared_frequency_clock=modem::helper_reference_shared_clock_interval(config,observed_helper,
                    plan.frequency_hz,out.uncertainty_hz,correction,
                    modem::HelperReferenceClockRelation::common_corrected_source);
                std::optional<modem::PatternHelperFrequencyWindow> execution_window;
                if(out.shared_frequency_clock) {
                    execution_window.emplace(modem::PatternHelperFrequencyWindow{reference_center,out.rf_half_width_hz,
                        plan.frequency_hz-config.carrier_hz,search.catalogue.clock_step_ppm/2,out.shared_frequency_clock});
                    const auto& window=*execution_window;
                    modem::PatternSearch declared;declared.hypotheses=search.catalogue.hypotheses;
                    declared.admitted_clock_half_width_ppm=search.catalogue.requested_clock_half_width_ppm;
                    declared.admitted_clock_cell_half_width_ppm=search.catalogue.clock_step_ppm/2;
                    modem::detail::PatternHelperFrequencySelector selector;
                    estimate_detail::check(stop);
                    if(!selector.initialize(declared,window))throw Error("Shared-radio clock forecast is unsupported by original runtime cells");
                    std::set<double> selected_clocks;
                    for(std::size_t i=0;i<search.catalogue.hypotheses.size();++i)if(selector.keep(i)) {
                        if((i&127U)==0)estimate_detail::check(stop);
                        ++out.shared_clock_selected_pairs;selected_clocks.insert(search.catalogue.hypotheses[i].clock_error_ppm);
                    }
                    out.shared_clock_selected_rates=selected_clocks.size();
                    if(!out.shared_clock_selected_pairs)throw Error("Shared-radio clock forecast has no selected original cells");
                    // Price this exact selection below. No smaller owner or stronger
                    // waveform replaces the original constructor/trial/continuation path.
                }
                auto channel=inputs.channel;
                const auto sample_snr=inputs.tx_dbm-inputs.path_loss_db-inputs.noise_density_dbm_hz-10*std::log10(config.sample_rate/2.);
                if(!std::isfinite(sample_snr)||sample_snr< -300||sample_snr>300)
                    throw Error("Payload sample SNR is outside the ordinary estimator numerical range");
                channel.snr_db=sample_snr;
                // Only the positive-band converter removes the common RF/helper LF
                // contribution. Raw folded PCM and a retained original prior keep the
                // actual channel CFO and ordinary real-image waveform unchanged.
                if(!search.original_prior_retained && !folded_reference)
                    channel.frequency_offset_hz=-plan.frequency_hz*channel.clock_error_ppm*1e-6;
                const auto estimate=simulation::estimate_with_receiver_search(one_bit_estimate(options),options,true,channel,
                    search.catalogue,1,100,inputs.receiver_work_mode,inputs.receiver_timing_model,&settings,stop,
                    execution_window?&*execution_window:nullptr,true);
                const auto point=cpu_point(target,estimate);
                out.receiver_cost=helper_receiver_cost(estimate);
                out.realtime_ratio=point.realtime_ratio;out.available=point.available;
                out.reason=precision_reason+(search.original_prior_retained?
                    "; conditional reference forecast; original oscillator prior retained; ordinary baseline, no NCO retuning or claimed RF gain":
                    folded_reference?
                    "; conditional raw mono Robust source; measured helper offset centers ordinary RF templates; no upstream NCO correction":
                    "; conditional same corrected Robust source")+
                    (out.shared_frequency_clock?
                    "; conditional shared-radio frequency-derived clock selects full-domain acquisition cells; ordinary constructor, backend/bin/workspace, logical trials, arrival/epoch alternatives and complete continuation retained":
                    "; full DATA-clock/UTC, arrival and epoch alternatives retained")+
                    "; ordinary reference-model receiver work, helper startup/tracking/conversion overhead excluded";
                if(!out.available)out.reason+="; residual receiver work/coverage/workspace is unsupported: "+estimate.receiver_work_assumptions;
            } catch(const Error& error) {out.reason=error.what();}
            return out;
        };
        static_cast<RealtimeHelperReceiverWork&>(out)=price_reference(
            out.precision.frequency_half_width_hz,out.precision.reason);
        if(out.precision.sustained_supported) {
            out.sustained_at_seconds=out.precision.sustained_at_seconds;
            try {
                if(out.precision.sustained_frequency_half_width_hz==out.uncertainty_hz)
                    out.sustained=static_cast<const RealtimeHelperReceiverWork&>(out);
                else out.sustained=price_reference(out.precision.sustained_frequency_half_width_hz,
                    "separate sustained conditional planning-model precision; reference must remain available");
            } catch(const std::bad_alloc&) {
                // A second optional price yields without erasing a valid
                // initial forecast or increasing any receiver allowance.
                out.sustained.reset();
            }
        }
    } catch(const Error& error) {out.reason=error.what();}
    catch(const std::bad_alloc&) {
        // Optional forecasting/work yields without retaining partial airtime,
        // precision or cost claims. The ordinary baseline is unaffected.
        out={};out.target_db_hz=target;
    }
    return out;
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
    // Separate cheap ordinary-work queries; no probability draws, source PCM,
    // historical masks or parallel broad helper bank are constructed.
    if(model.helper_reference.enabled)for(const auto& [target,point]:cpu_output) {
        (void)point;estimate_detail::check(stop);
        if(target==inputs.target_db_hz)model.helper_reference_cpu_points.push_back(model.helper_reference.conditional);
        else if(const auto config=try_resolve(inputs,target))
            model.helper_reference_cpu_points.push_back(realtime_helper_point(inputs,*config,target,cache.helper_precision,stop));
        else {RealtimeHelperCpuPoint gap;gap.target_db_hz=target;gap.reason="Target geometry is unavailable";
            model.helper_reference_cpu_points.push_back(std::move(gap));}
    }

}
}

std::optional<double> helper_ideal_tone_half_width(double cn0_db_hz,double observation_seconds) noexcept {
    if(!std::isfinite(cn0_db_hz) || !std::isfinite(observation_seconds) || !(observation_seconds>0))return {};
    // Three times sqrt(3/(2*pi^2*C/N0*T^3)). Evaluate in log space so
    // presentation cannot overflow intermediate C/N0 or T^3 values.
    const auto log_width=std::log(3.L)+.5L*std::log(3.L/(2*std::numbers::pi_v<long double>*
        std::numbers::pi_v<long double>))-.05L*cn0_db_hz*std::log(10.L)-1.5L*std::log(observation_seconds);
    const auto width=std::exp(log_width);
    if(!std::isfinite(width) || width<=0 || width>std::numeric_limits<double>::max())return {};
    const auto result=static_cast<double>(width);
    return result>0?std::optional<double>{result}:std::nullopt;
}
std::optional<double> helper_segmented_tone_half_width(double cn0_db_hz,
        std::span<const double> apertures) noexcept {
    if(!std::isfinite(cn0_db_hz) || apertures.empty())return {};
    long double information_time=0;
    for(const auto aperture:apertures) {
        if(!std::isfinite(aperture) || !(aperture>0))return {};
        const auto t=static_cast<long double>(aperture);
        information_time+=t*t*t;
    }
    if(!std::isfinite(information_time) || !(information_time>0))return {};
    const auto effective=std::cbrt(information_time);
    if(effective>std::numeric_limits<double>::max())return {};
    return helper_ideal_tone_half_width(cn0_db_hz,static_cast<double>(effective));
}
HelperReceiverCostLedger helper_receiver_cost(const simulation::Estimate& estimate) {
    HelperReceiverCostLedger out;
    out.finite_cpu_seconds=estimate.receiver_cpu_seconds;out.finite_audio_seconds=estimate.simulated_seconds;
    out.frontend_seconds=estimate.receiver_frontend_seconds;out.search_seconds=estimate.receiver_search_seconds;
    out.kernel_seconds=estimate.receiver_kernel_rebuild_seconds;out.continuation_seconds=estimate.tracking_seconds;
    out.payload_seconds=estimate.payload_processing_seconds;
    const auto components=out.frontend_seconds+out.search_seconds+out.kernel_seconds+
        out.continuation_seconds+out.payload_seconds;
    const auto nonnegative=[](double value){return std::isfinite(value)&&value>=0;};
    out.finite_available=estimate.receiver_work_supported&&nonnegative(out.finite_cpu_seconds)&&out.finite_audio_seconds>0&&
        std::isfinite(out.finite_audio_seconds)&&nonnegative(out.frontend_seconds)&&nonnegative(out.search_seconds)&&
        nonnegative(out.kernel_seconds)&&nonnegative(out.continuation_seconds)&&nonnegative(out.payload_seconds)&&
        components<=out.finite_cpu_seconds+1e-10*std::max(1.,out.finite_cpu_seconds);
    if(out.finite_available) {
        out.startup_other_seconds=std::max(0.,out.finite_cpu_seconds-components);
        out.finite_realtime_ratio=out.finite_cpu_seconds/out.finite_audio_seconds;
    }
    out.sustained_available=estimate.receiver_work_supported&&estimate.hardware_sustained_modeled&&
        nonnegative(estimate.hardware_sustained_realtime_ratio)&&
        nonnegative(estimate.hardware_sustained_tracking_realtime_ratio);
    out.sustained_realtime_ratio=estimate.hardware_sustained_realtime_ratio;
    out.sustained_continuation_ratio=estimate.hardware_sustained_tracking_realtime_ratio;
    out.initial_epoch_setup_seconds=estimate.initial_epoch_setup_seconds;
    out.new_epoch_setup_seconds=estimate.new_epoch_setup_seconds;
    out.new_epoch_frontend_seconds=estimate.new_epoch_frontend_seconds;
    out.initial_epochs=estimate.epoch_hypotheses;out.new_epoch_admissions=estimate.new_epoch_admissions;
    out.cohort_lifetime_seconds=estimate.hardware_cohort_lifetime_seconds;
    out.resident_instances=estimate.hardware_sustained_receiver_instances;
    out.workspace_supported=estimate.receiver_workspace_supported;out.geometry=estimate.receiver_geometry;
    return out;
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
        if(result.helper_reference.enabled)
            result.helper_reference.conditional=realtime_helper_point(inputs,config,inputs.target_db_hz,
                cache.impl_->curves.helper_precision,stop);

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
