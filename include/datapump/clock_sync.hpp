#pragma once
#include "datapump/types.hpp"
#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <limits>
#include <string>
#include <string_view>

namespace datapump::clock_sync {
inline constexpr double default_audio_error_seconds=.03;
inline void validate_audio_error(double seconds) {
    if(!std::isfinite(seconds) || seconds<=0 || seconds>60)
        throw Error("Audio error must be greater than zero and at most 60 seconds");
}
// PC UTC synchronization, independent of RF/audio frequency references.
// Accuracy is a worst-case bound at EACH station. Region is the total width
// about the propagation offset, not a one-sided distance allowance.
struct Policy {
    double accuracy_seconds = .001;
    double region_seconds = .4;
    double offset_seconds = 2.564;
    bool operator==(const Policy&) const = default;
    double half_window_seconds() const { return 2 * accuracy_seconds + region_seconds / 2; }
};
struct ArrivalMap {
    long double origin_samples=0,phase_scale=1,half_width_samples=0;
};
// Invert an explicitly supplied physical timing model without interpreting
// timestamp precision as device calibration. Error includes BOTH stations'
// GPS bounds, propagation region, transmitter presentation and capture delay.
// The map only constrains initial origin/phase; later symbols keep the complete
// original oscillator bank. Future clock stationarity remains an assumption.
inline std::optional<ArrivalMap> arrival_map(const Policy& policy,long double epoch,
        long double capture_utc,long double capture_sample,std::uint32_t sample_rate,
        double seconds_per_sample,double rate_uncertainty_fraction,double capture_error_seconds,
        std::uint64_t maximum_phase_samples,double transmit_error_seconds=default_audio_error_seconds) {
    if(!std::isfinite(policy.accuracy_seconds) || policy.accuracy_seconds<0 ||
       !std::isfinite(policy.region_seconds) || policy.region_seconds<0 ||
       !std::isfinite(policy.offset_seconds) || policy.offset_seconds<0 ||
       !sample_rate || !std::isfinite(epoch) || !std::isfinite(capture_utc) ||
       !std::isfinite(capture_sample) || !std::isfinite(seconds_per_sample) || seconds_per_sample<=0 ||
       !std::isfinite(rate_uncertainty_fraction) || rate_uncertainty_fraction<0 ||
       !std::isfinite(capture_error_seconds) || capture_error_seconds<0 ||
       !std::isfinite(transmit_error_seconds) || transmit_error_seconds<0)return std::nullopt;
    const auto tau=static_cast<long double>(seconds_per_sample);
    const auto delta=static_cast<long double>(rate_uncertainty_fraction)/sample_rate;
    if(delta>=tau)return std::nullopt;
    const auto d=epoch+policy.offset_seconds-capture_utc;
    const auto roundoff=64*std::numeric_limits<long double>::epsilon()*
        std::max({1.L,std::abs(epoch),std::abs(capture_utc)});
    const auto error=policy.half_window_seconds()+capture_error_seconds+transmit_error_seconds+
        .01L/sample_rate+roundoff;
    ArrivalMap result{capture_sample+d/tau,1/(sample_rate*tau),0};
    for(const auto p:{std::uint64_t{0},maximum_phase_samples})
        for(const auto sign:{-1,1})for(const auto denominator:{tau-delta,tau+delta}) {
            const auto corner=capture_sample+(d+static_cast<long double>(p)/sample_rate+sign*error)/denominator;
            result.half_width_samples=std::max(result.half_width_samples,
                std::abs(corner-(result.origin_samples+result.phase_scale*p)));
        }
    if(!std::isfinite(result.origin_samples) || !std::isfinite(result.half_width_samples) ||
       std::abs(result.origin_samples)+result.phase_scale*maximum_phase_samples+result.half_width_samples>=1e15L)
        return std::nullopt;
    result.half_width_samples=std::nextafter(result.half_width_samples,std::numeric_limits<long double>::infinity());
    return result;
}
inline double duration(std::string_view text) {
    while(!text.empty() && text.front()==' ')text.remove_prefix(1);
    while(!text.empty() && text.back()==' ')text.remove_suffix(1);
    double scale=1;
    if(text.ends_with("ns")){scale=1e-9;text.remove_suffix(2);}
    else if(text.ends_with("us")){scale=1e-6;text.remove_suffix(2);}
    else if(text.ends_with("ms")){scale=.001;text.remove_suffix(2);}
    else if(text.ends_with("s"))text.remove_suffix(1);
    else throw Error("Clock duration needs ns, us, ms, or s units");
    while(!text.empty() && text.back()==' ')text.remove_suffix(1);
    double value=0;
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(text.empty()||result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||
       !std::isfinite(value)||value<0||!std::isfinite(value*scale))
        throw Error("Clock duration must be finite and nonnegative");
    return value*scale;
}
inline std::string duration_text(double seconds) {
    if(!std::isfinite(seconds)||seconds<0)throw Error("Invalid clock duration");
    std::array<char,64> buffer{};
    const auto result=std::to_chars(buffer.data(),buffer.data()+buffer.size(),seconds*1000);
    if(result.ec!=std::errc{})throw Error("Cannot format clock duration");
    return std::string(buffer.data(),result.ptr)+"ms";
}
inline void validate(const Policy& policy) {
    for(const auto value:{policy.accuracy_seconds,policy.region_seconds,policy.offset_seconds})
        if(!std::isfinite(value)||value<0||value>32768)
            throw Error("Clock durations must be between zero and 32768 seconds");
    if(!std::isfinite(policy.half_window_seconds()))throw Error("Clock window overflow");
}
inline bool is_default(std::string_view text) {return text=="default"||text=="Default";}
inline std::optional<Policy> parse(std::string_view text) {
    if(is_default(text))return std::nullopt;
    if(!text.starts_with("GPS_"))throw Error("Clock sync needs default or GPS_ACCURACY-REGION_region-OFFSET_offset");
    text.remove_prefix(4);
    const auto unit_end=text.find("s-");
    const auto separator=unit_end==std::string_view::npos?unit_end:unit_end+1;
    if(separator==std::string_view::npos)throw Error("Clock sync is missing its region");
    Policy policy;policy.accuracy_seconds=duration(text.substr(0,separator));
    text.remove_prefix(separator+1);
    const auto region_end=text.find("_region-");
    if(region_end==std::string_view::npos||!text.ends_with("_offset"))throw Error("Invalid clock region/offset syntax");
    policy.region_seconds=duration(text.substr(0,region_end));
    text.remove_prefix(region_end+8);text.remove_suffix(7);
    policy.offset_seconds=duration(text);validate(policy);return policy;
}
inline std::string format(const std::optional<Policy>& policy) {
    if(!policy)return "default";
    validate(*policy);
    return "GPS_"+duration_text(policy->accuracy_seconds)+"-"+duration_text(policy->region_seconds)+
        "_region-"+duration_text(policy->offset_seconds)+"_offset";
}
}
