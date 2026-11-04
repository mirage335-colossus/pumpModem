#pragma once
#include <cstdint>
#include <optional>

namespace datapump::live::detail {
// Receiver-local policy, independent of a transmitter timestamp or metadata.
// Captured time only establishes the minimum observation interval. The caller
// still requires a fresh measured frequency bound before constructing a bank.
// No PCM, phase, payload or physical-END authority is retained here.
class HelperMeasurementAdmission {
public:
    static constexpr std::uint32_t measurement_seconds=36;
    void reset() noexcept {first_=UINT64_MAX;rate_=0;}
    bool observe(std::uint64_t first,std::uint64_t end,std::uint64_t captured_end,
                 std::uint32_t rate,bool qualified) noexcept {
        if(!qualified || !rate || first>=end || end>captured_end)return false;
        if(first_!=UINT64_MAX)return rate==rate_;
        if(end>UINT64_MAX-static_cast<std::uint64_t>(measurement_seconds)*rate)return false;
        // Detection becomes causal only once the complete scored support has
        // arrived. Never infer a transmitter onset from its earlier samples.
        first_=end;rate_=rate;return true;
    }
    std::optional<std::uint64_t> first_sample()const noexcept {
        return first_==UINT64_MAX?std::nullopt:std::optional<std::uint64_t>{first_};
    }
    // Outward conversion preserves the same captured date on another grid.
    // Readiness of newer ingress never authorizes an earlier buffered view.
    std::optional<std::uint64_t> deadline(std::uint32_t output_rate)const noexcept {
        if(first_==UINT64_MAX || !rate_ || !output_rate)return {};
        const auto raw=first_+static_cast<std::uint64_t>(measurement_seconds)*rate_;
        const auto whole=raw/rate_,remainder=raw%rate_;
        const auto tail=remainder*output_rate;
        const auto rounded=tail/rate_+(tail%rate_!=0);
        if(whole>(UINT64_MAX-rounded)/output_rate)return {};
        return whole*output_rate+rounded;
    }
    // Source-global view dates remain global through the realtime queue. A
    // new Bank owns only the suffix from its own source origin.
    static std::optional<std::uint64_t> local_sample(std::uint64_t source_sample,
                                                     std::uint64_t source_origin)noexcept {
        if(source_sample<source_origin)return {};
        return source_sample-source_origin;
    }
    bool ready(std::uint64_t view_first,std::uint32_t view_rate)const noexcept {
        const auto at=deadline(view_rate);return at && view_first>=*at;
    }
private:
    std::uint64_t first_=UINT64_MAX;
    std::uint32_t rate_=0;
};
}
