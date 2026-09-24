#pragma once
#include "datapump/pattern_code.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace datapump::gui::profile_reference {
struct Row {
    std::string label;
    bool active=false;
    // Boundary is the target at which this row starts while reducing C/N0.
    // The first automatic row instead includes targets at or above its boundary.
    std::optional<double> boundary_db_hz;
    unsigned nominal_chips=0;
    std::uint64_t chips=0;
    double seconds=0, bits_per_second=0;
    bool extended=false;
};
struct Model {
    std::vector<Row> rows;
    std::string tooltip;
};
namespace detail {
inline std::string compact(double value,int precision=3) {
    std::ostringstream out;out<<std::setprecision(precision)<<value;return out.str();
}
inline std::string fixed(double value,int precision) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;
    auto text=out.str();
    if(text.find('.')!=std::string::npos) {
        while(text.back()=='0')text.pop_back();
        if(text.back()=='.')text.pop_back();
    }
    return text;
}
inline std::string duration(double seconds) {
    if(seconds<.001)return compact(seconds*1e6)+"us";
    if(seconds<1)return fixed(seconds*1000,0)+"ms";
    if(seconds<60)return fixed(seconds,2)+"s";
    if(seconds<3600)return compact(seconds/60)+"min";
    return compact(seconds/3600)+"h";
}
inline std::string quantities(const Row& row) {
    const auto chips=row.chips<1000000?std::to_string(row.chips):compact(static_cast<double>(row.chips));
    const auto rate=row.bits_per_second>=.01 && row.bits_per_second<100000?
        fixed(row.bits_per_second,2):compact(row.bits_per_second);
    return chips+"complex "+duration(row.seconds)+" "+rate+"bit/s";
}
inline Row row(const modem::Config& config) {
    Row result;result.nominal_chips=config.spreading_factor;
    result.chips=modem::pattern_chips_per_symbol(config);
    result.seconds=modem::symbol_seconds(config);
    result.bits_per_second=modem::bit_rate(config);
    result.extended=config.integration_seconds>0;
    return result;
}
inline bool automatic(tuning::PatternMode mode) {
    return mode==tuning::PatternMode::auto_pattern || mode==tuning::PatternMode::auto_keystream ||
        mode==tuning::PatternMode::auto_tone;
}
}

// Presentation only. Resolve the actual clock/carrier/key geometry through the
// tuner, including its short-pattern floors, rather than copying those rules.
// Build on settings changes; the bounded search performs no waveform work.
inline Model build(const modem::Config& base,double target_db_hz,tuning::PatternMode mode,bool encryption) {
    if(!std::isfinite(target_db_hz))throw Error("target C/N0 must be finite dB-Hz");
    const auto resolve=[&](double target) {
        return tuning::receive_profiles(base,std::array<double,1>{target},mode,encryption).front();
    };
    // Every automatic floor has been reached by 200 dB-Hz. This also permits
    // a valid stronger transmit target without exceeding receive-list limits.
    // Forced profiles do not depend on target strength. Keep finite manual
    // targets outside the receiver list's range valid for this display too.
    const auto selected=resolve(detail::automatic(mode)?std::min(target_db_hz,200.):
        std::clamp(target_db_hz,-200.,200.));
    Model model;
    model.tooltip="Target C/N0 boundaries in dB-Hz, rounded to 0.01 dB. Read downward: each < row applies until the next boundary. "
        "The highlighted row is the current transmit profile. complex = complex I/Q chips per raw bit, including any partial final chip; "
        "duration and bit/s are raw symbol values before message overhead. Boundaries follow the selected rate, carrier, clock, pattern mode and keys. "
        "Below the final boundary, integration grows continuously. These are tuning boundaries, not measured receive-confidence thresholds.";
    if(!detail::automatic(mode)) {
        auto row=detail::row(selected);row.active=true;row.label="fixed "+detail::quantities(row);
        model.rows.push_back(std::move(row));
        model.tooltip="Forced pattern length: target C/N0 does not multiply the pattern. "+model.tooltip;
        return model;
    }

    // The public tuner's bandwidth range puts every finite-to-extended
    // transition above -60 dB-Hz. At that probe, even its fastest clock fits
    // the existing 64-bit symbol sample counter. The endpoints are search
    // bounds, not a second copy of the tuner's pattern or energy thresholds.
    double upper=200.;auto current=resolve(upper);
    std::optional<double> preceding_boundary;
    for(unsigned step=0;step<32;++step) {
        double lower=-60.,higher=upper;
        for(unsigned iteration=0;iteration<64;++iteration) {
            const auto midpoint=lower+(higher-lower)/2;
            if(midpoint==lower || midpoint==higher)break;
            const auto candidate=resolve(midpoint);
            if(candidate.integration_seconds==0 && candidate.spreading_factor==current.spreading_factor)higher=midpoint;
            else lower=midpoint;
        }
        const auto boundary=higher;
        auto row=detail::row(current);
        row.active=selected.integration_seconds==0 && selected.spreading_factor==current.spreading_factor;
        row.boundary_db_hz=preceding_boundary.value_or(boundary);
        row.label=(preceding_boundary?"<":">=")+detail::fixed(*row.boundary_db_hz,2)+"dB-Hz "+detail::quantities(row);
        model.rows.push_back(std::move(row));
        auto next=resolve(lower);
        if(next.integration_seconds>0) {
            Row extended;
            extended.extended=true;extended.boundary_db_hz=boundary;
            extended.active=selected.integration_seconds>0;
            if(extended.active) {
                extended=detail::row(selected);extended.active=true;extended.boundary_db_hz=boundary;
            }
            extended.label="<"+detail::fixed(boundary,2)+"dB-Hz "+
                (extended.active?detail::quantities(extended):"longer integration");
            model.rows.push_back(std::move(extended));
            return model;
        }
        preceding_boundary=boundary;upper=lower;current=std::move(next);
    }
    throw Error("automatic profile reference exceeds its bounded row count");
}
}
