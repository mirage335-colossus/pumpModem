#pragma once
#include "datapump/live.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::live::detail {
// Audits have their own cadence. The accepted-link lease and initial probation
// remain shorter, so a lost or unproductive reference restores broad work first.
inline std::uint64_t helper_audit_period(std::uint32_t rate,std::uint64_t symbol) noexcept {
    const auto symbols=symbol>UINT64_MAX/8?UINT64_MAX:8*symbol;
    return std::max<std::uint64_t>(6ULL*rate,symbols);
}
inline std::uint64_t helper_audit_deadline(std::uint64_t frontier,std::uint32_t rate,
                                         std::uint64_t symbol) noexcept {
    const auto period=helper_audit_period(rate,symbol);
    return frontier>UINT64_MAX-period?UINT64_MAX:frontier+period;
}

// The optional tone fit may account for the public occupied data band, never
// decoded contents or the current frame. Include every receive profile and the
// full requested RF/audio-clock domain, even when the ordinary bank is limited.
inline std::optional<helper::OccupiedBand> helper_occupied_receive_band(const Settings& settings) noexcept {
    try {
        std::vector<modem::Config> profiles;
        const auto add=[&](bool keyed) {
            if(settings.transfer.automatic_receive_profiles) {
                auto family=tuning::receive_profiles(settings.transfer.modem,settings.transfer.receive_targets_db_hz,
                    settings.transfer.receive_pattern_mode,keyed);
                profiles.insert(profiles.end(),family.begin(),family.end());
            } else profiles.push_back(settings.transfer.modem);
        };
        if(settings.permits_plaintext())add(false);
        if(settings.transfer.key || !settings.receive_keys.empty())add(true);
        if(profiles.empty())return std::nullopt;
        if(settings.long_message_modem)profiles.push_back(*settings.long_message_modem);
        double lower=std::numeric_limits<double>::infinity(),upper=0;
        for(const auto& profile:profiles) {
            const auto half=helper::occupied_bandwidth(profile)/2;
            // The default local-clock expansion has a different coupled
            // policy. Until explicitly represented here, keep its raw fit.
            if(!profile.oscillator_search)return std::nullopt;
            const auto clock_policy=clock_sync::effective_policy(settings.transfer.clock_sync,
                transfer::shared_hop_calendar(profile));
            const auto search=modem::oscillator_pattern_search(profile,
                clock_policy?modem::utc_transmit_rate_limit(profile):0);
            const auto rf=search.frequency.requested_half_width_hz;
            const auto clock=search.requested_clock_half_width_ppm;
            const auto uncertainty=rf+(std::abs(profile.carrier_hz)+half)*clock*1e-6;
            const auto low=profile.carrier_hz-half-uncertainty;
            const auto high=profile.carrier_hz+half+uncertainty;
            if(!std::isfinite(low)||!std::isfinite(high)||low<=0||high<=low)return std::nullopt;
            lower=std::min(lower,low);upper=std::max(upper,high);
        }
        if(!std::isfinite(lower)||upper<=lower)return std::nullopt;
        return helper::OccupiedBand{lower,upper};
    } catch(...) {
        return std::nullopt;
    }
}

inline long double helper_leading_seconds(std::uint64_t physical_origin,std::uint32_t rate) {
    if(!rate)throw Error("Helper Carrier lead has no source sample clock");
    const auto epoch=physical_origin/rate;
    const auto origin=static_cast<long double>(physical_origin%rate)/rate;
    const auto parity=static_cast<long double>(epoch%2);
    const auto count=(parity+origin-2.L)*2.5L,nearest=std::round(count);
    const auto snapped=std::abs(count-nearest)<=8*std::numeric_limits<long double>::epsilon()*
        std::max(1.L,std::abs(count))?nearest:count;
    const auto start=std::floor(snapped)*.4L-parity;
    return origin-start;
}
// Move every observation date together when capture is cleared. Zero is the
// frequency component's same-as-phase sentinel, not an independently dated
// observation at the replacement bank's origin.
inline std::optional<helper::Reference> rebase_helper_reference(helper::Reference reference,
        std::uint64_t origin,std::uint64_t frontier,std::uint32_t rate) {
    if(!rate || reference.sample_rate!=rate || reference.sample<origin || reference.sample>frontier)
        return std::nullopt;
    if(reference.frequency_sample) {
        if(reference.frequency_sample<=origin || reference.frequency_sample>reference.sample)
            return std::nullopt;
        reference.frequency_sample-=origin;
    }
    reference.sample-=origin;
    reference.dwell_start_seconds-=static_cast<double>(origin)/rate;
    return reference;
}

// Loss of an optional phase measurement does not change the oscillator already
// used for a partially scored Robust symbol. Continue its last accepted affine
// model until ordinary payload tracking/absence scoring releases that receiver.
struct HelperPhaseContinuation {
    std::uint64_t sample=0;
    double phase=0,frequency_hz=0;
    double at(std::uint64_t at,std::uint32_t rate)const {
        if(!rate)throw Error("helper phase continuation has no sample clock");
        const auto delta=at>=sample?static_cast<long double>(at-sample):
            -static_cast<long double>(sample-at);
        return static_cast<double>(static_cast<long double>(phase)+
            2*std::numbers::pi_v<long double>*frequency_hz*delta/rate);
    }
};

// Both local transmit geometries share one helper frequency. Selecting a long
// message or editing the callsign cannot move the carrier into the other band.
inline double helper_occupied_bandwidth(const Settings& settings) {
    const auto occupied=[](const modem::Config& c) {
        return helper::occupied_bandwidth(c);
    };
    auto bandwidth=occupied(settings.transfer.modem);
    if(settings.long_message_modem)bandwidth=std::max(bandwidth,occupied(*settings.long_message_modem));
    return bandwidth;
}
}
