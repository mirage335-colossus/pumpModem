#pragma once

#include "datapump/crypto.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace datapump::modem {

inline unsigned sched_hop_multiplier(std::string_view value) {
    if (value == "disabled" || value == "off" || value == "1") return 1;
    if (value == "10") return 10;
    if (value == "100") return 100;
    if (value == "1k" || value == "1000") return 1000;
    if (value == "10k" || value == "10000") return 10000;
    throw Error("scheduled-hop multiplier must be disabled, 10, 100, 1k or 10k");
}
inline std::string_view sched_hop_id(unsigned multiplier) {
    switch (multiplier) {
    case 1: return "disabled";
    case 10: return "10";
    case 100: return "100";
    case 1000: return "1k";
    case 10000: return "10k";
    default: throw Error("invalid scheduled-hop time multiplier");
    }
}

// Public geometry for a secret UTC active/null calendar. This primitive neither
// gates PCM nor changes waveform, symbol, carrier, or observed-absence clocks.
// Its caller must make those mappings explicit before enabling transmission.
struct SchedHopPlan {
    unsigned time_multiplier = 1; // Total base hops per frame; 1 means disabled.
    std::uint64_t group_hops = 1; // Consecutive ACTIVE 2/5-second base hops.
    // Clock bounds are +/- per endpoint; region is total width, Audio error
    // is nonnegative lag [0,A], and offset delays the receiver's calendar only.
    double clock_error_seconds = .01, peer_clock_error_seconds = .01;
    double region_seconds = 0, offset_seconds = 0, audio_error_seconds = .05;
    double ramp_seconds = .01;

    bool enabled() const { return time_multiplier != 1; }
    bool continuous() const { return group_hops == time_multiplier; }
    double relative_clock_region_seconds() const {
        return clock_error_seconds + peer_clock_error_seconds + region_seconds/2;
    }
    void validate() const {
        if (time_multiplier != 1 && time_multiplier != 10 && time_multiplier != 100 &&
            time_multiplier != 1000 && time_multiplier != 10000)
            throw Error("invalid scheduled-hop time multiplier");
        if (!group_hops || group_hops > time_multiplier)
            throw Error("scheduled-hop active count must be between one and the frame hop count");
        for (const auto value : {clock_error_seconds,peer_clock_error_seconds,region_seconds,offset_seconds})
            if (!std::isfinite(value) || value < 0 || value > 32768)
                throw Error("invalid scheduled-hop clock/propagation allowance");
        if (!std::isfinite(audio_error_seconds) || audio_error_seconds < 0 || audio_error_seconds > 60)
            throw Error("invalid scheduled-hop audio lag allowance");
        if (!std::isfinite(ramp_seconds) || ramp_seconds <= 0 || ramp_seconds > 32768)
            throw Error("invalid scheduled-hop ramp duration");
    }
};

namespace sched_hop_detail {
inline std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
        throw Error("scheduled-hop coordinate would overflow");
    return a + b;
}
inline std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > std::numeric_limits<std::uint64_t>::max() / b)
        throw Error("scheduled-hop coordinate would overflow");
    return a * b;
}
// Every frame receives a fixed draw budget. Rejection is unbiased; exhaustion
// fails explicitly instead of borrowing another frame's bytes or applying a
// modulo-biased fallback. The input is big-endian, as in the FHSS permutation.
inline std::uint64_t choice(std::span<const std::uint8_t> bytes, std::uint64_t count) {
    if (!count) throw Error("invalid scheduled-hop choice count");
    const auto threshold = (std::uint64_t{0} - count) % count;
    for (std::size_t offset = 0; offset <= bytes.size() && bytes.size() - offset >= 8; offset += 8) {
        std::uint64_t value = 0;
        for (unsigned j = 0; j < 8; ++j) value = (value << 8) | bytes[offset + j];
        if (value >= threshold) return value % count;
    }
    throw Error("scheduled-hop draw budget exhausted");
}
struct WipeBytes {
    Bytes& bytes;
    ~WipeBytes() {
        // Do not leave the derived child root in an ordinary freed vector.
        volatile std::uint8_t* data = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i) data[i] = 0;
    }
};
} // namespace sched_hop_detail

struct SchedHopFrame {
    std::uint64_t index = 0, begin_hop = 0, active_begin_hop = 0,
                  active_end_hop = 0, end_hop = 0;
};

class SchedHopSchedule {
public:
    // Immutable and safe to share. Cache returned frames in the owning stream,
    // not a mutable shared calendar: random access never restarts a byte stream.
    static constexpr std::size_t frame_bytes = 256;

    explicit SchedHopSchedule(SchedHopPlan plan, const Crypto& key) : plan_(plan) {
        plan_.validate();
        period_ = plan_.time_multiplier;
        jitter_ = (period_ - plan_.group_hops) / 2;
        if (plan_.enabled()) {
            // The public geometry is an injective (epoch, byte-offset) context.
            // It derives a child root using only the dedicated FHSS purpose.
            // Changing either M or G cannot reuse another geometry's choices.
            auto seed = key.stream(StreamPurpose::Fhss, plan_.group_hops,
                static_cast<std::uint64_t>(plan_.time_multiplier) * 32, 32,
                StreamDomain::SchedHopKeyV1);
            sched_hop_detail::WipeBytes wipe{seed};
            key_.emplace(seed);
        }
    }
    const SchedHopPlan& plan() const { return plan_; }
    bool enabled() const { return plan_.enabled(); }
    std::uint64_t frame_hops() const { return period_; }
    std::uint64_t maximum_jitter_hops() const { return jitter_; }
    // Exact worst-case bounds between the end of one active group and the
    // beginning of the next. They include adversarial adjacent keyed choices.
    std::uint64_t minimum_null_hops() const { return period_ - plan_.group_hops - jitter_; }
    std::uint64_t maximum_null_hops() const {
        return sched_hop_detail::add(period_ - plan_.group_hops, jitter_);
    }

    SchedHopFrame frame(std::uint64_t index) const {
        using namespace sched_hop_detail;
        const auto begin = multiply(index, period_);
        const auto end = add(begin, period_);
        std::uint64_t selected = 0;
        if (key_) {
            // Here epoch is a versioned frame coordinate, not a payload UTC
            // second. Each complete frame owns a fresh independent epoch key.
            auto bytes = key_->stream(StreamPurpose::Fhss, index, 0, frame_bytes,
                                      StreamDomain::SchedHopUtcV1);
            WipeBytes wipe{bytes};
            selected = choice(bytes, jitter_ + 1);
        }
        const auto active = add(begin, selected);
        return {index, begin, active, add(active, plan_.group_hops), end};
    }
    bool is_active(std::uint64_t hop) const {
        if (!enabled()) return true;
        const auto f = frame(hop / period_);
        return hop >= f.active_begin_hop && hop < f.active_end_hop;
    }
    // Number of complete active base hops strictly before this physical hop.
    // On an active hop, select(rank(hop)) == hop. On a null hop it selects the
    // next active hop. Neither operation iterates over historical frames.
    std::uint64_t rank(std::uint64_t hop) const {
        if (!enabled()) return hop;
        using namespace sched_hop_detail;
        const auto f = frame(hop / period_);
        const auto within = hop <= f.active_begin_hop ? 0 :
            std::min(hop - f.active_begin_hop, plan_.group_hops);
        return add(multiply(f.index, plan_.group_hops), within);
    }
    std::uint64_t select(std::uint64_t active_rank) const {
        if (!enabled()) return active_rank;
        const auto f = frame(active_rank / plan_.group_hops);
        return sched_hop_detail::add(f.active_begin_hop, active_rank % plan_.group_hops);
    }
    std::uint64_t first_active_at_or_after(std::uint64_t hop) const { return select(rank(hop)); }

    // Exact floor/ceil(2*Fs*hop/5), with no accumulated rounded-dwell error.
    // Full physical end coordinates must fit; unrepresentable future frames
    // and samples fail explicitly instead of wrapping to earlier keystreams.
    static std::uint64_t sample_boundary(std::uint64_t hop, std::uint32_t fs, bool up = false) {
        if (!fs) throw Error("invalid scheduled-hop sample rate");
        using namespace sched_hop_detail;
        const auto twice_rate = 2ULL * fs;
        const auto whole = multiply(hop / 5, twice_rate);
        const auto fraction = (hop % 5) * twice_rate;
        return add(whole, (fraction + (up ? 4 : 0)) / 5);
    }
    // Inverse of floor-rounded sample boundaries, including their rounded
    // edges. Equivalent to ceil(5*(sample+1)/(2*Fs))-1 without sample+1 overflow.
    static std::uint64_t hop_containing_sample(std::uint64_t sample, std::uint32_t fs) {
        if (!fs) throw Error("invalid scheduled-hop sample rate");
        using namespace sched_hop_detail;
        const auto denominator = 2ULL * fs;
        const auto whole = multiply(sample / denominator, 5);
        const auto fraction = (sample % denominator + 1) * 5;
        const auto upper = add(whole, fraction / denominator + (fraction % denominator != 0));
        return upper - 1;
    }
    std::uint64_t minimum_null_samples(std::uint32_t fs) const {
        return sample_boundary(minimum_null_hops(), fs);
    }

private:
    SchedHopPlan plan_;
    std::uint64_t period_ = 0, jitter_ = 0;
    std::optional<Crypto> key_;
};

// Keep absolute UTC/sample coordinates integral. Only the local fractional
// residue uses floating arithmetic; subtract integers before converting a
// distance, including on platforms where long double is ordinary FP64.
struct SchedHopCoordinate {
    std::uint64_t sample = 0;
    long double fraction = 0;

    void validate() const {
        if (!std::isfinite(fraction) || fraction < 0 || fraction >= 1)
            throw Error("invalid scheduled-hop fractional coordinate");
    }
    SchedHopCoordinate add_samples(std::uint64_t samples) const {
        validate();
        return {sched_hop_detail::add(sample,samples),fraction};
    }
    SchedHopCoordinate add_signed(long double delta) const {
        validate();
        if (!std::isfinite(delta)) throw Error("invalid scheduled-hop coordinate adjustment");
        // Separate the large integral adjustment BEFORE adding the existing
        // fractional residue. Otherwise even an exactly integral large delta
        // can erase a small residue on FP64 long-double implementations.
        const auto integral = std::trunc(delta);
        if (!std::isfinite(integral) || std::abs(integral) >= std::ldexp(1.L,64))
            throw Error("scheduled-hop coordinate adjustment would overflow");
        auto next = sample;
        if (integral >= 0) next = sched_hop_detail::add(next,static_cast<std::uint64_t>(integral));
        else {
            const auto remove = static_cast<std::uint64_t>(-integral);
            if (remove > next) throw Error("scheduled-hop coordinate would precede UTC origin");
            next -= remove;
        }
        auto residue = fraction+(delta-integral);
        if (residue < 0) {
            if (!next) throw Error("scheduled-hop coordinate would precede UTC origin");
            --next;residue += 1;
        }
        if (residue >= 1) { next = sched_hop_detail::add(next,1);residue -= 1; }
        return {next,residue};
    }
    long double distance(const SchedHopCoordinate& other) const {
        validate();other.validate();
        const auto whole = sample >= other.sample ? static_cast<long double>(sample-other.sample) :
            -static_cast<long double>(other.sample-sample);
        return whole + fraction - other.fraction;
    }
};

struct SchedHopLocation {
    bool active = false;
    bool continuous = false;
    SchedHopCoordinate source;
    // Group intervals are half open. The fixed integer source quantum is
    // floor(2*Fs/5) per active base hop; extra fractional-boundary samples are
    // placed at the END of the group, outside its smoothly gated output.
    std::uint64_t physical_begin = 0, physical_end = 0;
    std::uint64_t source_begin = 0, source_end = 0;
    std::uint64_t flat_begin = 0, flat_end = 0;
    std::uint64_t rise_begin = 0, fall_end = 0;
    std::uint64_t hop = 0, frame_index = 0;
};

// Piecewise-affine physical/source map for split symbols. This changes neither
// the source's pulse shaping nor its bit position. The physical carrier clock
// MUST remain physical; only coefficient/pulse position follows source time.
// The bounded exact frame cache belongs to one sequential stream/bank, not a
// shared concurrently accessed calendar. Frame index identifies each entry;
// plan, key and sample rate are immutable for the lifetime of this object.
// Copies retain the exact entries and diagnostic counters, then mutate their
// own storage independently. Cache exhaustion only recomputes an exact frame.
class SchedHopSourceMap {
public:
    static constexpr std::size_t cached_frame_capacity = 8;
    struct CacheStatistics {
        // Saturating diagnostic counters; never used to choose keyed output.
        // Misses count attempted constructions. Bytes count successful frame
        // draws, excluding the geometry-specific child-key setup at creation.
        std::uint64_t hits = 0, misses = 0, generated_bytes = 0;
    };
    SchedHopSourceMap(SchedHopPlan plan,const Crypto& key,std::uint32_t sample_rate)
        : schedule_(plan,key),rate_(sample_rate) {
        if (!rate_) throw Error("invalid scheduled-hop source sample rate");
        quantum_ = 2ULL*rate_/5;
        group_samples_ = sched_hop_detail::multiply(quantum_,plan.group_hops);
        if (continuous()) return;
        if (!quantum_) throw Error("scheduled-hop source rate has no whole sample per hop");
        const auto bound = static_cast<long double>(rate_)*plan.relative_clock_region_seconds();
        left_ = static_cast<std::uint64_t>(std::ceil(bound));
        right_ = static_cast<std::uint64_t>(std::ceil(bound+static_cast<long double>(rate_)*plan.audio_error_seconds));
        ramp_ = static_cast<std::uint64_t>(std::ceil(static_cast<long double>(rate_)*plan.ramp_seconds));
        if (left_ >= group_samples_ || right_ >= group_samples_-left_ ||
            ramp_ >= (group_samples_-left_-right_+1)/2)
            throw Error("scheduled-hop clock/audio guards and smooth ramps leave no full-power active samples");
    }
    const SchedHopPlan& plan() const { return schedule_.plan(); }
    const SchedHopSchedule& schedule() const { return schedule_; }
    bool continuous() const { return !plan().enabled() || plan().continuous(); }
    std::uint32_t sample_rate() const { return rate_; }
    std::uint64_t source_samples_per_hop() const { return quantum_; }
    std::uint64_t source_samples_per_group() const { return group_samples_; }
    std::size_t working_bytes() const { return sizeof(*this); }
    CacheStatistics cache_statistics() const { return cache_statistics_; }
    // Resetting diagnostics neither invalidates entries nor rewinds a stream.
    void reset_cache_statistics() const { cache_statistics_ = {}; }
    std::uint64_t minimum_flat_samples() const {
        return continuous() ? std::numeric_limits<std::uint64_t>::max() :
            group_samples_-left_-right_-2*ramp_;
    }
    SchedHopLocation locate(std::uint64_t physical_sample,long double fraction = 0) const {
        return locate(SchedHopCoordinate{physical_sample,fraction});
    }
    SchedHopLocation locate(SchedHopCoordinate physical) const {
        physical.validate();
        if (continuous()) {
            SchedHopLocation result;
            result.active = result.continuous = true;result.source = physical;
            result.physical_end = result.source_end = result.flat_end = result.fall_end =
                std::numeric_limits<std::uint64_t>::max();
            return result;
        }
        // The floor-boundary inverse can select the next frame up to one
        // sample before its ceil-rounded begin. Both sides map to the same
        // source boundary during that null sliver, so no address is consumed.
        const auto hop = SchedHopSchedule::hop_containing_sample(physical.sample,rate_);
        const auto& f = frame(hop/schedule_.frame_hops());
        return locate_in_frame(physical,hop,f);
    }
    // START/right inverse skips a null interval; END/left inverse publishes a
    // completed symbol at the preceding active end, before a long quiet gap.
    SchedHopCoordinate source_to_physical(SchedHopCoordinate source,bool end = false) const {
        source.validate();
        if (continuous()) return source;
        auto q = source.sample/group_samples_;
        auto within = source.sample%group_samples_;
        if (end && !within && source.fraction == 0 && q) {
            const auto& f = frame(q-1);
            const auto begin = SchedHopSchedule::sample_boundary(f.active_begin_hop,rate_,true);
            return {sched_hop_detail::add(begin,group_samples_),0};
        }
        const auto& f = frame(q);
        const auto begin = SchedHopSchedule::sample_boundary(f.active_begin_hop,rate_,true);
        // Do not return a start that locate() would reject because the same
        // complete active group cannot fit the physical/source address space.
        (void)sched_hop_detail::add(begin,group_samples_);
        (void)sched_hop_detail::add(sched_hop_detail::multiply(q,group_samples_),group_samples_);
        return {sched_hop_detail::add(begin,within),source.fraction};
    }
    // Query another position in an already returned immutable group without
    // crypto, allocation, or cache mutation. This also covers its quiet prefix
    // and tail; callers must obtain the next frame once its bounds are passed.
    static double gain(const SchedHopLocation& group,SchedHopCoordinate physical) {
        physical.validate();
        if (group.continuous) return 1;
        if (physical.sample < group.rise_begin || physical.sample >= group.fall_end) return 0;
        if (physical.sample >= group.flat_begin && physical.sample < group.flat_end) return 1;
        long double unit;
        if (physical.sample < group.flat_begin)
            unit = physical.distance({group.rise_begin,0}) /
                static_cast<long double>(group.flat_begin-group.rise_begin);
        else unit = SchedHopCoordinate{group.fall_end,0}.distance(physical) /
                static_cast<long double>(group.fall_end-group.flat_end);
        const auto t = std::clamp(static_cast<double>(unit),0.,1.);
        return t*t*t*(10+t*(-15+6*t));
    }

private:
    static void count(std::uint64_t& counter,std::uint64_t increment = 1) {
        counter += std::min(increment,std::numeric_limits<std::uint64_t>::max()-counter);
    }
    const SchedHopFrame& frame(std::uint64_t q) const {
        // Most lookups stay in one frame. Checking the most recent slot first
        // keeps that path constant-time; neighboring timing/frequency searches
        // reuse the remaining entries without generating the same AES stream.
        std::size_t position = 0;
        while (position < cached_size_ && cached_frames_[cached_order_[position]].index != q)
            ++position;
        std::uint8_t slot;
        if (position < cached_size_) {
            count(cache_statistics_.hits);
            slot = cached_order_[position];
        } else {
            count(cache_statistics_.misses);
            // Construct before evicting: an overflow/exhaustion failure leaves
            // all prior entries intact and cannot wrap to an earlier address.
            const auto generated = schedule_.frame(q);
            if (schedule_.enabled()) count(cache_statistics_.generated_bytes,SchedHopSchedule::frame_bytes);
            if (cached_size_ < cached_frame_capacity) {
                slot = cached_size_++;
            } else {
                position = cached_frame_capacity-1;
                slot = cached_order_[position];
            }
            cached_frames_[slot] = generated;
        }
        for (; position; --position) cached_order_[position] = cached_order_[position-1];
        cached_order_[0] = slot;
        return cached_frames_[slot];
    }
    SchedHopLocation locate_in_frame(SchedHopCoordinate physical,std::uint64_t hop,const SchedHopFrame& f) const {
        using namespace sched_hop_detail;
        SchedHopLocation result;
        result.hop = hop;result.frame_index = f.index;
        result.physical_begin = SchedHopSchedule::sample_boundary(f.active_begin_hop,rate_,true);
        result.physical_end = add(result.physical_begin,group_samples_);
        result.source_begin = multiply(f.index,group_samples_);
        result.source_end = add(result.source_begin,group_samples_);
        result.rise_begin = add(result.physical_begin,left_);
        result.flat_begin = add(result.rise_begin,ramp_);
        result.fall_end = result.physical_end-right_;
        result.flat_end = result.fall_end-ramp_;
        result.active = physical.sample >= result.physical_begin && physical.sample < result.physical_end;
        if (physical.sample < result.physical_begin) result.source = {result.source_begin,0};
        else if (physical.sample >= result.physical_end) result.source = {result.source_end,0};
        else result.source = {add(result.source_begin,physical.sample-result.physical_begin),physical.fraction};
        return result;
    }
    SchedHopSchedule schedule_;
    std::uint32_t rate_ = 0;
    std::uint64_t quantum_ = 0,group_samples_ = 0,left_ = 0,right_ = 0,ramp_ = 0;
    mutable std::array<SchedHopFrame,cached_frame_capacity> cached_frames_{};
    mutable std::array<std::uint8_t,cached_frame_capacity> cached_order_{};
    mutable std::uint8_t cached_size_ = 0;
    mutable CacheStatistics cache_statistics_;
};

} // namespace datapump::modem
