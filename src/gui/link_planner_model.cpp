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
ReceiverSupport receiver_support(const Inputs& inputs,const modem::Config& config) {
    auto options=inputs.options;options.modem=config;
    auto channel=inputs.channel;
    channel.snr_db=std::clamp(inputs.tx_dbm-inputs.path_loss_db-inputs.noise_density_dbm_hz-
        10*std::log10(config.sample_rate/2.),-300.,300.);
    // Workspace and clock coverage depend on the selected geometry and local
    // banks, not payload length. This analytical one-symbol estimate allocates
    // neither a draft, a transfer probe, nor PCM inside the bounded search.
    transfer::Estimate one;one.wire_bits=1;one.total_seconds=seconds(config);
    const auto estimate=simulation::estimate(one,options,true,channel);
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
    std::map<std::uint64_t,ReceiverSupport> support_cache;

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
        if(const auto found=support_cache.find(samples);found!=support_cache.end())return found->second;
        return support_cache.emplace(samples,receiver_support(inputs,*config)).first->second;
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
    ClockSearch(const Inputs& value,double low,double high):inputs(value),minimum(low),maximum(high) {
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
        // strength input does not alter the search or workspace checks used
        // here; no success probability is displayed by the planner.
        channel.snr_db=std::clamp(sample_snr,-300.,300.);
        const auto receiver=simulation::estimate(transmission,options,true,channel);
        result.clock_search_supported=receiver.carrier_in_search;
        result.receiver_workspace_supported=receiver.receiver_workspace_supported;
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
