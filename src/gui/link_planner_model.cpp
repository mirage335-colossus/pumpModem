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
bool clock_fits(const Inputs& inputs,const modem::Config& config) {
    const auto offset=inputs.channel.frequency_offset_hz+
        config.carrier_hz*inputs.channel.clock_error_ppm*1e-6;
    return std::abs(offset)<=modem::default_pattern_frequency_search(config).half_width_hz;
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
std::optional<double> clock_boundary(const Inputs& inputs,double minimum,double maximum) {
    // Search from the longest durations. Short profiles switch search policy
    // at 16 seconds, so coverage is not globally monotonic in target C/N0.
    constexpr unsigned steps=192;
    auto previous=minimum;
    const auto first=try_resolve(inputs,previous);
    if(first && clock_fits(inputs,*first))return std::nullopt;
    for(unsigned i=1;i<=steps;++i) {
        const auto target=minimum+(maximum-minimum)*i/steps;
        const auto config=try_resolve(inputs,target);
        if(config && clock_fits(inputs,*config)) {
            auto high=target,low=previous;
            for(unsigned j=0;j<48;++j) {
                const auto middle=(low+high)/2;
                const auto candidate=try_resolve(inputs,middle);
                if(candidate && clock_fits(inputs,*candidate))high=middle;
                else low=middle;
            }
            return high;
        }
        previous=target;
    }
    return std::nullopt;
}
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
        if(!receiver.carrier_in_search)result.receiver_status="Clock outside RX search";
        if(!receiver.receiver_workspace_supported) {
            if(!result.receiver_status.empty())result.receiver_status+=" · ";
            result.receiver_status+="More memory needed";
        }
        if(result.receiver_status.empty())result.receiver_status="Clock search fits · reception unverified";

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
            result.clock_target=clock_boundary(inputs,minimum_target,maximum_target);
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
