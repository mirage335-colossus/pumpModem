#pragma once
#include "datapump/crypto.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <limits>

namespace datapump::modem {
// A TX-only, immutable tuning plan. Fake and a future radio adapter must consume
// the same keyed schedule; presentation must never invent a second hop order.
// This implements schedule selection, NOT radio control, settling or compliance.
struct FhssPlan {
    static constexpr unsigned capacity=200;
    std::array<double,capacity> shifts{};
    unsigned channels=0;
    double dwell_seconds=0;
    void validate() const {
        if(channels<2 || channels>capacity || !std::isfinite(dwell_seconds) || dwell_seconds<=0)
            throw Error("invalid FHSS tuning plan");
        for(unsigned i=0;i<channels;++i) {
            if(!std::isfinite(shifts[i]) || shifts[i]<0)throw Error("invalid FHSS Shift");
            for(unsigned j=0;j<i;++j)if(shifts[i]==shifts[j])throw Error("duplicate FHSS Shift");
        }
    }
};
namespace fhss_detail {
// Separate deterministic draw kernel for independent boundary/rejection tests.
inline std::size_t permutation(unsigned channels,std::span<const std::uint8_t> bytes,
        std::array<unsigned,FhssPlan::capacity>& next) {
    if(channels<2||channels>FhssPlan::capacity)throw Error("invalid FHSS channel count");
    for(unsigned i=0;i<channels;++i)next[i]=i;
    std::size_t used=0;
    for(unsigned i=channels-1;i>0;--i) {
        const std::uint64_t bound=i+1;
        const auto limit=(std::uint64_t{1}<<32)/bound*bound;
        std::uint32_t word;
        do {
            if(used+4>bytes.size())throw Error("FHSS permutation draw budget exhausted");
            word=0;for(unsigned j=0;j<4;++j)word=(word<<8)|bytes[used++];
        } while(word>=limit); // Unbiased integer selection, not modulo bias.
        std::swap(next[i],next[word%bound]);
    }
    return used;
}
}
class FhssSchedule {
public:
    std::shared_ptr<const FhssPlan> plan;
    std::array<unsigned,FhssPlan::capacity> order{};
    std::array<std::uint8_t,32> prefix{};
    std::size_t prefix_size=0;
    std::uint64_t epoch=0,cycle=0,byte_offset=0;
    std::size_t consumed_bytes=0;
    // Each cycle owns a disjoint region, including bounded rejection reserve.
    // No counter wrap or fallback to an earlier order is permitted.
    static constexpr std::uint64_t cycle_bytes=1024;
    static constexpr std::uint64_t maximum_cycle=std::numeric_limits<std::uint64_t>::max()/cycle_bytes;
    FhssSchedule(std::shared_ptr<const FhssPlan> value,const Crypto& key,std::uint64_t start):
        plan(std::move(value)),epoch(start),key_(key) {
        if(!plan)throw Error("missing FHSS tuning plan");
        plan->validate();
        select_cycle(0);
    }
    // Generation-relative sample coordinate, including surrounding noise.
    // Absolute UTC scheduling/driver latency belong to the playback adapter.
    std::uint64_t ordinal(std::uint64_t sample,std::uint32_t rate) const {
        // Round the configured dwell to its double sample-domain value once.
        // Dividing seconds first can place exact integer boundaries a sample late.
        const double dwell_samples=rate*plan->dwell_seconds;
        if(!rate || !std::isfinite(dwell_samples) || dwell_samples<=0)throw Error("invalid FHSS sample timeline");
        const auto value=std::floor(static_cast<long double>(sample)/dwell_samples);
        if(!rate || !std::isfinite(value) || value>=std::ldexp(1.L,64))throw Error("FHSS sample timeline overflow");
        return static_cast<std::uint64_t>(value);
    }
    unsigned channel(std::uint64_t hop) {
        const auto wanted=hop/plan->channels;
        if(wanted!=cycle)select_cycle(wanted);
        return order[hop%plan->channels];
    }
private:
    Crypto key_;
    void select_cycle(std::uint64_t wanted) {
        if(wanted>maximum_cycle)throw Error("FHSS cycle stream exhausted");
        const auto offset=wanted*cycle_bytes;
        auto bytes=key_.stream(StreamPurpose::Fhss,epoch,offset,cycle_bytes,StreamDomain::FhssCycleV2);
        std::array<unsigned,FhssPlan::capacity> next{};
        std::size_t used;
        try { used=fhss_detail::permutation(plan->channels,bytes,next); }
        catch(...) { std::fill(bytes.begin(),bytes.end(),0);throw; }
        // Commit only after a complete successful construction. The single cache
        // is valid solely for this immutable key/epoch/plan and exact cycle.
        // Seeking never generates all intervening cycles or retains their bytes.
        order=next;cycle=wanted;byte_offset=offset;consumed_bytes=used;
        prefix_size=std::min(prefix.size(),used);
        std::copy_n(bytes.begin(),prefix_size,prefix.begin());
        std::fill(bytes.begin(),bytes.end(),0);
    }
};
}
