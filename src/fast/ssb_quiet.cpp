#include "ssb_quiet.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::fast::detail {

std::vector<SsbPublicGroupSymbols> ssb_public_group_symbols(const SingleCarrierSchedule& schedule,
                                                            std::uint64_t ordinal) {
    constexpr auto maximum=std::numeric_limits<std::size_t>::max();
    if(ordinal>=maximum)throw std::length_error("Fast SSB group ordinal exceeds local geometry");
    const auto index=static_cast<std::size_t>(ordinal);
    const auto before=schedule.total_interval_symbols(index);
    if(before>maximum-schedule.training_count)
        throw std::length_error("Fast SSB group position exceeds local geometry");
    auto position=before+schedule.training_count;
    if(schedule.interval_symbols(index)>maximum-position)
        throw std::length_error("Fast SSB group extent exceeds local geometry");
    std::vector<SsbPublicGroupSymbols> groups;
    const auto add=[&](std::size_t count) {
        groups.push_back({position,position+count});
        position+=count;
    };
    const auto marker=schedule.marker_symbols(index);
    if(marker) {
        add(marker/2);
        add(marker-marker/2);
    }
    for(auto left=schedule.data_symbols;left;) {
        const auto data=std::min(left,schedule.pilot_stride);
        add(data+schedule.pilot_count);
        left-=data;
    }
    return groups;
}

std::vector<SsbQuietUnit> ssb_quiet_units(std::size_t group_count) {
    std::vector<SsbQuietUnit> units;
    for(std::size_t first=0;first<group_count;) {
        const auto remaining=group_count-first;
        const auto count=remaining<=ssb_quiet_maximum_groups?remaining:ssb_quiet_minimum_groups;
        units.push_back({first,first+count});
        first+=count;
    }
    return units;
}

SsbPublicPresence classify_ssb_quiet_unit(std::span<const SsbQuietGroupProjection> groups,bool exact_zero,
                                           std::optional<double> noise_power,SsbQuietUnitDiagnostics* diagnostics) {
    if(groups.empty() || groups.size()>ssb_quiet_maximum_groups)
        throw std::invalid_argument("Invalid Fast SSB quiet unit geometry");
    if(diagnostics) {
        *diagnostics={};
        diagnostics->count=groups.size();
    }
    const auto noise=noise_power.value_or(0.);
    if(!std::isfinite(noise) || noise<0.)return SsbPublicPresence::unscored;
    double quality_sum=0.,amplitude_sum=0.,power=0.,raw_energy=0.,projected_energy=0.;
    std::size_t represented=0,informative=0,centers=0;
    bool strong_fragment=false;
    for(std::size_t i=0;i<groups.size();++i) {
        const auto& group=groups[i];
        const auto raw=group.raw_template_energy;
        const auto pilot=group.projected_template_energy;
        const auto y=group.projected_input_energy;
        const auto cross_squared=std::norm(group.cross);
        if(!group.centers || group.centers>std::numeric_limits<std::size_t>::max()-centers ||
           !std::isfinite(raw) || !std::isfinite(pilot) || !std::isfinite(y) || !std::isfinite(cross_squared) ||
           raw<0. || pilot<0. || y<0.)
            return SsbPublicPresence::unscored;
        centers+=group.centers;
        raw_energy+=raw;
        projected_energy+=pilot;
        const auto observable=pilot>0. && pilot>=.05*raw;
        auto* detail=diagnostics?&diagnostics->groups[i]:nullptr;
        if(detail)*detail={group.centers,raw,pilot,y,cross_squared,0.,0.,observable,false};
        informative+=observable;
        if(!(raw>0.) || !(pilot>0.))continue;
        ++represented;
        if(!(y>0.))continue;
        const auto quality=std::clamp(cross_squared/(y*pilot),0.,1.);
        const auto amplitude=std::abs(group.cross)/pilot;
        const auto strong=quality>=.80 && amplitude>=.10 && y/group.centers>noise;
        strong_fragment|=strong;
        if(detail) {
            detail->squared_coherence=quality;
            detail->amplitude=amplitude;
            detail->strong=strong;
        }
        quality_sum+=quality;
        amplitude_sum+=amplitude;
        power+=y;
    }
    const auto quality=quality_sum/groups.size();
    if(!std::isfinite(raw_energy) || !std::isfinite(projected_energy) || !std::isfinite(power) ||
       !std::isfinite(quality) || !std::isfinite(amplitude_sum))return SsbPublicPresence::unscored;
    // Each constituent retains a measured contribution. Use the aggregate
    // identifiable pilot energy to admit the complete observation unit.
    const auto observable=represented==groups.size() && projected_energy>=.05*raw_energy;
    if(diagnostics) {
        diagnostics->informative=informative;
        diagnostics->observable=observable;
        diagnostics->projected_to_raw_energy=raw_energy>0.?projected_energy/raw_energy:0.;
        diagnostics->mean_squared_coherence=quality;
        diagnostics->mean_amplitude=amplitude_sum/groups.size();
    }
    if(strong_fragment)return SsbPublicPresence::present;
    if(!observable)return SsbPublicPresence::no_detection;
    // Per-group projection magnitudes keep a phase discontinuity from
    // cancelling evidence of a continuing public waveform.
    if(quality>=.40 && amplitude_sum>=.10*groups.size() && power/centers>noise)
        return SsbPublicPresence::present;
    if(exact_zero || (groups.size()>=ssb_quiet_minimum_groups && quality<=.30))
        return SsbPublicPresence::absent;
    return SsbPublicPresence::no_detection;
}

SsbQuietRun::SsbQuietRun(std::uint32_t input_rate,std::size_t center_stride)
    : required_ticks_(std::uint64_t(input_rate)*6),stride_(center_stride) {
    if(!required_ticks_ || !stride_)
        throw std::invalid_argument("Fast SSB quiet timing requires a native sample grid");
}

bool SsbQuietRun::reset() noexcept {
    start_.reset();
    past_last_.reset();
    return false;
}

bool SsbQuietRun::observe(SsbAcquisitionView view,SsbCenterRange group,SsbPublicPresence presence,
                          std::optional<double> calibrated_power_ceiling) {
    if(completion_)return true;
    if(view.stride!=stride_ || group.past_last<=group.first ||
       (group.past_last-group.first)%stride_)
        throw std::invalid_argument("Invalid Fast SSB quiet group geometry");
    if(presence==SsbPublicPresence::present || presence==SsbPublicPresence::unscored ||
       group.first<view.first_center_input_tick)
        return reset();
    const auto offset=group.first-view.first_center_input_tick;
    if(offset%stride_)throw std::invalid_argument("Fast SSB quiet group is off the native sample grid");
    const auto first=offset/stride_;
    const auto count=(group.past_last-group.first)/stride_;
    if(first>view.samples.size() || count>view.samples.size()-first)return reset();

    if(presence==SsbPublicPresence::no_detection) {
        if(!calibrated_power_ceiling || !std::isfinite(*calibrated_power_ceiling) ||
           *calibrated_power_ceiling<0.)
            return reset();
        double power=0.;
        for(const auto sample:view.samples.subspan(static_cast<std::size_t>(first),static_cast<std::size_t>(count)))
            power+=std::norm(sample);
        power/=static_cast<double>(count);
        if(!std::isfinite(power) || power>*calibrated_power_ceiling)return reset();
    }
    if(!past_last_ || *past_last_!=group.first)start_=group.first;
    past_last_=group.past_last;
    if(elapsed_input_ticks()>=required_ticks_)completion_=group.past_last;
    return completion_.has_value();
}

std::uint64_t SsbQuietRun::elapsed_input_ticks() const noexcept {
    return start_?*past_last_-*start_:0;
}

std::optional<SsbCenterRange> SsbQuietRun::observed_run() const noexcept {
    if(!start_)return {};
    return SsbCenterRange{*start_,*past_last_};
}

} // namespace datapump::fast::detail
