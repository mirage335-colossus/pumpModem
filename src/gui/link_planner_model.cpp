#include "link_planner_model.hpp"
#include "datapump/lpi_estimate.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/simulation_estimate.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numeric>
#include <sstream>
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
    return tuning::receive_profiles(inputs.options.modem,targets,inputs.mode,
                                    inputs.options.key.has_value()).front();
}
double seconds(const modem::Config& config) {
    return static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate;
}
std::optional<modem::Config> try_resolve(const Inputs& inputs,double target) {
    try { return resolve(inputs,target); }
    catch(const Error&) { return std::nullopt; }
}
struct ReceiverSupport {
    bool clock=false,workspace=false;
    bool fits() const {return clock&&workspace;}
};
ReceiverSupport receiver_support(const Inputs& inputs,const modem::Config& config,
        std::span<const modem::Config> profiles={}) {
    auto options=inputs.options;options.modem=config;
    auto channel=inputs.channel;
    channel.snr_db=std::clamp(inputs.tx_dbm-inputs.path_loss_db-inputs.noise_density_dbm_hz-
        10*std::log10(config.sample_rate/2.),-300.,300.);
    // Workspace and clock coverage depend on the selected geometry and local
    // banks, not payload length. This analytical one-symbol estimate allocates
    // neither a draft, a transfer probe, nor PCM inside the bounded search.
    transfer::Estimate one;one.wire_bits=1;one.total_seconds=seconds(config);
    const auto estimate=simulation::estimate(one,options,true,channel,profiles,1,false);
    return {estimate.carrier_in_search,estimate.receiver_workspace_supported};
}

// All searches have fixed iteration counts. Preserve discrete tuner steps:
// a boundary is the weakest target whose actual sampled duration meets it.
std::optional<double> duration_boundary(const Inputs& inputs,double minimum,double maximum,double limit) {
    const auto weak=try_resolve(inputs,minimum),strong=try_resolve(inputs,maximum);
    if(!weak || !strong || seconds(*weak)<=limit || seconds(*strong)>limit)return std::nullopt;
    for(unsigned i=0;i<56;++i) {
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
    std::map<std::pair<std::uint64_t,double>,ReceiverSupport> support_cache;
    std::span<const double> companion_targets;
    std::optional<ReceiveBanks> receive_banks;

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
        const auto config=try_resolve(inputs,target);
        if(!config)return {};
        const auto samples=modem::symbol_sample_count(*config);
        // Mixed public/private families can cross different short-profile
        // floors at the same selected sample count, so retain target identity
        // when an explicit receive-bank context is supplied.
        const auto cache_key=std::pair{samples,receive_banks?target:0.};
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
        return support_cache.emplace(cache_key,receiver_support(inputs,*config,profiles)).first->second;
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
            std::span<const double> companions={},std::optional<ReceiveBanks> banks=std::nullopt):
        inputs(value),minimum(low),maximum(high),companion_targets(companions),receive_banks(banks) {
        const auto chip=modem::pattern_chip_samples(inputs.options.modem);
        alignment=std::gcd(chip,std::max<std::uint64_t>(1,chip/2));
        // Divisor work is bounded by the validated rate/sample-clock geometry,
        // never by the number of bits or their simulated duration.
        for(std::uint64_t divisor=1;divisor<=alignment/divisor;++divisor)if(alignment%divisor==0) {
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
                const auto requested=base.carrier_hz*modem::default_clock_uncertainty_ppm*1e-6L;
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
transfer::Estimate one_bit_estimate(const modem::Config& config) {
    const auto symbol=modem::symbol_sample_count(config);
    auto count=add_samples(modem::training_sample_count(config),symbol);
    count=add_samples(count,modem::pattern_pulse_padding_samples(config));
    count=add_samples(count,modem::pattern_pulse_padding_samples(config));
    count=add_samples(count,modem::suppression_sample_count(config));
    transfer::Estimate result;
    result.wire_bits=result.coded_bytes=result.content_bytes=1;
    result.content_seconds=result.coded_seconds=seconds(config);
    result.total_seconds=static_cast<double>(count)/config.sample_rate;
    result.waveform_samples=count<=std::numeric_limits<std::size_t>::max()?static_cast<std::size_t>(count):0;
    return result;
}
auto curve_key(const transfer::Options& options,const modem::ChannelConfig& channel) {
    const auto c=options.key?transfer::seeded_config(options,options.modem.stream_epoch):options.modem;
    // Only quantities read by the estimator enter this bounded cache. The
    // private key's derived pattern identity is retained; the sampled-channel
    // RNG seed does not choose probability draws.
    return std::tuple{c.pattern_symbols,c.stream_epoch,c.stream_phase_samples,c.sample_rate,
        c.constellation_bits,c.carrier_hz,c.bandwidth_hz,c.training_seconds,
        modem::symbol_sample_count(c),modem::pattern_chip_samples(c),c.spreading_mode,c.pulse_shaping,c.scramble,c.dsss,
        c.spreading_seed,c.dsss_seed,c.memory_limit,options.dsp_workspace_bytes,
        options.timestamp,options.search_seconds,channel.snr_db,channel.frequency_offset_hz,
        channel.delay_samples,channel.clock_error_ppm,channel.phase_noise_degrees_per_sqrt_second};
}
using CurveKey=decltype(curve_key(transfer::Options{},modem::ChannelConfig{}));
struct CurveEntry {
    CurveKey key;
    simulation::Estimate estimate;
    bool probability_computed=false;
};
std::vector<CurveEntry>& curve_cache() {
    // A GUI edit can reuse the curve independently of selected target and
    // draft length. Storage never grows with symbol duration or graph width.
    thread_local std::vector<CurveEntry> entries;
    return entries;
}
CurveEntry& curve_entry(const transfer::Options& options,const modem::ChannelConfig& channel) {
    auto& entries=curve_cache();
    const auto key=curve_key(options,channel);
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.key==key;});
    if(found!=entries.end())return *found;
    if(entries.size()==512)entries.erase(entries.begin());
    entries.push_back({key,simulation::estimate(one_bit_estimate(options.modem),options,true,channel,{},1,false),false});
    return entries.back();
}
ReceivePoint receive_point(double target,const simulation::Estimate& estimate,bool numerical_range) {
    return {target,estimate.success_probability,estimate.confidence_available&&numerical_range,
        estimate.carrier_in_search,estimate.receiver_workspace_supported};
}
CpuPoint cpu_point(double target,const simulation::Estimate& estimate) {
    const auto ratio=estimate.simulated_seconds>0?estimate.receiver_cpu_seconds/estimate.simulated_seconds:0;
    return {target,ratio,estimate.carrier_in_search&&estimate.receiver_workspace_supported&&
        std::isfinite(ratio)&&ratio>0};
}
void receive_curve(Model& model,const simulation::Estimate& selected_one) {
    if(model.points.empty())return;
    const auto& inputs=model.inputs;
    const auto low=model.points.front().target_db_hz,high=model.points.back().target_db_hz;
    constexpr unsigned expensive_limit=12,coarse_limit=6,refinement_limit=32;
    unsigned expensive=0;
    std::map<double,ReceivePoint> output;
    std::map<double,CpuPoint> cpu_output;
    const auto evaluate=[&](double target,bool permit_expensive) -> bool {
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
        auto& entry=curve_entry(options,channel);
        // Receiver work is already available from the cheap support check;
        // the denser CPU curve adds no statistical trials or sampled audio.
        cpu_output.emplace(target,cpu_point(target,entry.estimate));
        if(!entry.estimate.carrier_in_search||!entry.estimate.receiver_workspace_supported||!numerical_range) {
            output.emplace(target,receive_point(target,entry.estimate,numerical_range));return true;
        }
        const bool costly=entry.estimate.drift_sections>1;
        if(costly&&(!permit_expensive||expensive>=expensive_limit))return false;
        // Count chosen expensive sample locations, including cache hits, so
        // identical inputs produce an identical curve on every repaint.
        if(costly)++expensive;
        if(!entry.probability_computed) {
            entry.estimate=simulation::estimate(one_bit_estimate(*geometry),options,true,channel);
            entry.probability_computed=true;
        }
        output.emplace(target,receive_point(target,entry.estimate,numerical_range));return true;
    };
    const auto probe=[&](double target,bool permit_expensive) {
        bool added=evaluate(target,permit_expensive);
        const auto exact=output.find(target);
        if(model.automatic_mode&&exact!=output.end()&&!exact->second.confidence_available) {
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
                    const auto& support=curve_entry(options,channel).estimate;
                    if(snr>=-300&&snr<=300&&support.carrier_in_search&&support.receiver_workspace_supported) {
                        output.erase(target);cpu_output.erase(target);
                    }
                }
            }
        }
        return added;
    };
    // The selected one-bit probability comes from the already evaluated
    // headline, including when that headline describes a longer draft.
    output.emplace(inputs.target_db_hz,receive_point(inputs.target_db_hz,selected_one,model.confidence_available));
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
            if(!next->second.confidence_available)continue;
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
}
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

Model build(const Inputs& inputs) {
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
        const auto overhead=static_cast<std::uint64_t>(transmission.waveform_samples)-symbol;
        if(inputs.wire_bits>(std::numeric_limits<std::uint64_t>::max()-overhead)/symbol)
            throw Error("planned duration exceeds the modem sample counter");
        const auto payload=static_cast<std::uint64_t>(inputs.wire_bits)*symbol;
        const auto waveform=add_samples(payload,overhead);
        const auto finish=add_samples(waveform,modem::pattern_absence_samples(config));
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
        result.low_audio_hz=config.carrier_hz-result.occupied_bandwidth_hz/2;
        result.high_audio_hz=config.carrier_hz+result.occupied_bandwidth_hz/2;
        const auto observer=lpi::estimate(transmission,options);
        result.observer_available=observer.status==lpi::Status::available;
        result.observer_hypothetical=observer.hypothetical_encryption;
        result.observer_ratio=observer.equivalent_symbols;

        auto channel=inputs.channel;
        const auto sample_snr=result.actual_cn0_db_hz-10*std::log10(config.sample_rate/2.);
        // The existing simulator accepts only -300..300 dB. Clamping its
        // strength input does not alter search or workspace checks. Keep
        // confidence unavailable outside the estimator's supported range.
        channel.snr_db=std::clamp(sample_snr,-300.,300.);
        const auto receiver=simulation::estimate(transmission,options,true,channel);
        result.clock_search_supported=receiver.carrier_in_search;
        result.receiver_workspace_supported=receiver.receiver_workspace_supported;
        result.confidence_available=receiver.confidence_available&&sample_snr>=-300&&sample_snr<=300;
        result.success_probability=result.confidence_available?receiver.success_probability:0;
        result.one_bit_success_probability=result.confidence_available?receiver.one_bit_success_probability:0;
        result.phase_coherence_loss_db=receiver.phase_coherence_loss_db;
        result.coherent_reference_only=receiver.coherent_reference_only;
        result.drift_model_available=receiver.drift_model_available&&result.confidence_available;
        result.coherent_success_probability=result.confidence_available?receiver.coherent_success_probability:0;
        result.section_phase_coherence_loss_db=receiver.section_phase_coherence_loss_db;
        auto single_receiver=inputs.wire_bits==1?receiver:
            simulation::estimate(single_transmission,options,true,channel,{},1,false);
        // Draft length changes continuation and total work, but acquisition
        // already supplies this one-bit probability without another trial run.
        single_receiver.success_probability=receiver.one_bit_success_probability;
        single_receiver.confidence_available=receiver.confidence_available;
        single_receiver.drift_model_available=receiver.drift_model_available;
        single_receiver.coherent_reference_only=receiver.coherent_reference_only;
        result.one_bit_cpu_seconds=single_receiver.cpu_seconds;
        result.receiver_cpu_seconds=single_receiver.receiver_cpu_seconds;
        result.cpu_realtime_ratio=single_receiver.simulated_seconds>0?
            single_receiver.receiver_cpu_seconds/single_receiver.simulated_seconds:0;
        result.cpu_per_bit_ratio=single_receiver.cpu_seconds/result.bit_seconds;
        result.one_bit_cpu_available=single_receiver.receiver_workspace_supported&&
            std::isfinite(result.one_bit_cpu_seconds)&&std::isfinite(result.receiver_cpu_seconds)&&
            std::isfinite(result.cpu_realtime_ratio)&&result.one_bit_cpu_seconds>0&&single_receiver.simulated_seconds>0;
        auto& selected_cache=curve_entry(options,channel);
        selected_cache.estimate=single_receiver;selected_cache.probability_computed=true;
        if(!receiver.carrier_in_search)result.receiver_status="Clock outside RX search";
        if(!receiver.receiver_workspace_supported) {
            if(!result.receiver_status.empty())result.receiver_status+=" · ";
            result.receiver_status+="Wide RX search exceeds RAM";
            if(inputs.dsp_workspace_percent)result.receiver_status+=" ("+std::to_string(inputs.dsp_workspace_percent)+"%)";
        }
        if(result.receiver_status.empty())result.receiver_status="Clock and RAM fit · reception unverified";

        // Leave sample-counter headroom for one symbol of physical absence
        // and the fixed waveform overhead even at the graph's weakest end.
        const auto maximum_seconds=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())/
            (4.L*config.sample_rate);
        const auto minimum_target=std::max(-200.,tuning::pattern_target_symbol_snr_db-
            10*std::log10(static_cast<double>(maximum_seconds)));
        constexpr double maximum_target=200;
        if(result.automatic_mode) {
            result.fast_target=duration_boundary(inputs,minimum_target,maximum_target,1);
            result.day_target=duration_boundary(inputs,minimum_target,maximum_target,seconds_per_day);
            const auto choices=ClockSearch(inputs,minimum_target,maximum_target).run();
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
            const auto geometry=try_resolve(inputs,target);
            if(!geometry)continue;
            auto point_options=inputs.options;point_options.modem=*geometry;
            Point point;point.target_db_hz=target;point.bit_seconds=seconds(*geometry);
            transfer::Estimate one;one.wire_bits=1;one.total_seconds=point.bit_seconds;
            const auto estimate=lpi::estimate(one,point_options);
            point.observer_available=estimate.status==lpi::Status::available;
            point.observer_ratio=estimate.equivalent_symbols;
            result.points.push_back(point);
        }
        receive_curve(result,single_receiver);
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
