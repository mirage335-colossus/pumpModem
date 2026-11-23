#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace datapump::audio::detail {
inline std::vector<std::uint32_t> rate_candidates(std::uint32_t logical_rate, std::uint32_t minimum_rate=0) {
    if(logical_rate<64 || logical_rate>120000000)throw Error("invalid logical sample rate");
    constexpr std::array<std::uint32_t,13> common{8000,11025,16000,22050,24000,32000,44100,48000,88200,96000,176400,192000,384000};
    // Sound-card interfaces stay within actual audio clock ranges. SDR-rate
    // logical PCM remains separate and does not imply an SDR audio backend.
    std::vector<std::uint32_t> result;
    if(logical_rate>=8000 && logical_rate<=384000)result.push_back(logical_rate);
    // Preserve physical passband when a faster device clock is available.
    for(const auto rate:common)if(rate>logical_rate)result.push_back(rate);
    for(auto it=common.rbegin();it!=common.rend();++it)if(*it<logical_rate)result.push_back(*it);
    if(minimum_rate) {
        std::erase_if(result,[&](const auto rate){return rate<minimum_rate;});
        if(result.empty())throw Error("no native audio sample rate meets the requested minimum of "+std::to_string(minimum_rate)+" Hz");
    }
    return result;
}
}
