#include "datapump/helper_delayed_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::helper {
namespace {
constexpr std::size_t max_samples=4U*1024U*1024U,max_blocks=65536U;
constexpr std::uint64_t max_delay_ns=60'000'000'000ULL;
bool finite_nonnegative(long double x){return std::isfinite(x)&&x>=0;}
}
HelperDelayedBuffer::HelperDelayedBuffer(Config c):config_(c) {
    if(!c.maximum_samples||c.maximum_samples>max_samples||!c.maximum_blocks||c.maximum_blocks>max_blocks||
       c.delay_nanoseconds>max_delay_ns||!finite_nonnegative(c.clock_jump_tolerance_nanoseconds))
        throw std::invalid_argument("invalid Helper delayed-buffer bounds");
    samples_.resize(c.maximum_samples);blocks_.resize(c.maximum_blocks);
}
HelperDelayedBuffer::PushResult HelperDelayedBuffer::push(const CaptureBlock& m,std::span<const std::complex<float>> data){
    if(!m.sample_count||m.sample_count>config_.maximum_samples||
       (m.samples_present?data.size()!=m.sample_count:!data.empty())||
       !std::isfinite(m.sample_period_nanoseconds)||m.sample_period_nanoseconds<=0||
       !finite_nonnegative(m.capture_uncertainty_nanoseconds)||!finite_nonnegative(m.sample_period_uncertainty_nanoseconds)||
       m.sample_count>std::numeric_limits<std::uint64_t>::max()-m.first_sample)
        return PushResult::invalid;
    if(m.transmitter_time.valid&&(!std::isfinite(m.transmitter_time.seconds_at_first_sample)||
       !std::isfinite(m.transmitter_time.seconds_per_sample)||m.transmitter_time.seconds_per_sample<=0||
       !finite_nonnegative(m.transmitter_time.uncertainty_seconds)||
       !std::isfinite(m.transmitter_time.reference_fractional_sample)||
       m.transmitter_time.reference_fractional_sample<0||m.transmitter_time.reference_fractional_sample>=1))return PushResult::invalid;
    const auto end=m.first_sample+static_cast<std::uint64_t>(m.sample_count);
    if(have_previous_&&(m.first_sample<previous_end_||m.capture_monotonic_nanoseconds<previous_.capture_monotonic_nanoseconds))
        return PushResult::out_of_order;
    const long double nominal_elapsed=static_cast<long double>(m.sample_count-1)*
        (m.sample_period_nanoseconds+m.sample_period_uncertainty_nanoseconds);
    const long double round_up=4*std::numeric_limits<long double>::epsilon()*std::abs(nominal_elapsed);
    const long double elapsed=m.capture_end_monotonic_valid?0:std::ceil(nominal_elapsed+round_up);
    constexpr auto limit=std::numeric_limits<std::uint64_t>::max();
    // Strictly less is intentional: on platforms where long double has only
    // 53 mantissa bits, UINT64_MAX rounds to 2^64; <= could admit an invalid cast.
    if(!finite_nonnegative(elapsed)||elapsed>=static_cast<long double>(limit-m.capture_monotonic_nanoseconds))return PushResult::invalid;
    if(m.capture_end_monotonic_valid&&m.capture_end_monotonic_nanoseconds<m.capture_monotonic_nanoseconds)
        return PushResult::invalid;
    const auto nominal_end=m.capture_end_monotonic_valid?m.capture_end_monotonic_nanoseconds:
        m.capture_monotonic_nanoseconds+static_cast<std::uint64_t>(elapsed);
    if(m.capture_monotonic_uncertainty_nanoseconds>limit-nominal_end)return PushResult::invalid;
    const auto end_time=nominal_end+m.capture_monotonic_uncertainty_nanoseconds;
    if(config_.delay_nanoseconds>limit-end_time)return PushResult::invalid;
    if(block_count_==blocks_.size()||(m.samples_present&&m.sample_count>samples_.size()-sample_count_))return PushResult::full;
    Entry entry;entry.capture=m;entry.end_monotonic_nanoseconds=end_time;
    entry.eligible_nanoseconds=end_time+config_.delay_nanoseconds;
    if(have_previous_){
        entry.missing_before=m.first_sample-previous_end_;
        const auto delta=static_cast<long double>(m.first_sample-previous_.first_sample)*previous_.sample_period_nanoseconds;
        const auto index_delta=static_cast<long double>(m.first_sample-previous_.first_sample);
        const auto eps=std::numeric_limits<long double>::epsilon();
        const auto system_round=4*eps*(std::abs(static_cast<long double>(previous_.capture_system_nanoseconds))+
            std::abs(static_cast<long double>(m.capture_system_nanoseconds))+std::abs(delta));
        const auto monotonic_round=4*eps*(std::abs(static_cast<long double>(previous_.capture_monotonic_nanoseconds))+
            std::abs(static_cast<long double>(m.capture_monotonic_nanoseconds))+std::abs(delta));
        const auto period_error=index_delta*previous_.sample_period_uncertainty_nanoseconds;
        const auto system_tolerance=config_.clock_jump_tolerance_nanoseconds+previous_.capture_uncertainty_nanoseconds+
            m.capture_uncertainty_nanoseconds+period_error+system_round;
        const auto monotonic_tolerance=config_.clock_jump_tolerance_nanoseconds+
            static_cast<long double>(previous_.capture_monotonic_uncertainty_nanoseconds)+
            static_cast<long double>(m.capture_monotonic_uncertainty_nanoseconds)+period_error+monotonic_round;
        const long double system_expected=static_cast<long double>(previous_.capture_system_nanoseconds)+delta;
        const long double monotonic_expected=static_cast<long double>(previous_.capture_monotonic_nanoseconds)+delta;
        entry.clock_discontinuity=!std::isfinite(delta)||!std::isfinite(system_tolerance)||!std::isfinite(monotonic_tolerance)||
            m.capture_clock_epoch!=previous_.capture_clock_epoch||
            std::abs(static_cast<long double>(m.capture_system_nanoseconds)-system_expected)>system_tolerance||
            std::abs(static_cast<long double>(m.capture_monotonic_nanoseconds)-monotonic_expected)>monotonic_tolerance;
    }
    if(m.transmitter_time.valid){
        if(have_reference_&&(m.transmitter_time.reference_sample!=retained_reference_||
                            m.transmitter_time.reference_fractional_sample!=retained_reference_fractional_sample_)){
            entry.reference_mismatch=true;entry.capture.transmitter_time.valid=false;
        }else if(!have_reference_){retained_reference_=m.transmitter_time.reference_sample;
            retained_reference_fractional_sample_=m.transmitter_time.reference_fractional_sample;have_reference_=true;}
    }
    if(m.samples_present){
        const auto tail=(sample_head_+sample_count_)%samples_.size();
        const auto first=std::min(data.size(),samples_.size()-tail);
        std::copy_n(data.begin(),first,samples_.begin()+static_cast<std::ptrdiff_t>(tail));
        std::copy(data.begin()+static_cast<std::ptrdiff_t>(first),data.end(),samples_.begin());
        sample_count_+=data.size();
    }
    blocks_[(block_head_+block_count_)%blocks_.size()]=entry;++block_count_;
    previous_=m;previous_end_=end;have_previous_=true;
    return entry.reference_mismatch?PushResult::reference_mismatch:PushResult::accepted;
}
HelperDelayedBuffer::PopResult HelperDelayedBuffer::pop(std::uint64_t now,std::uint64_t frontier,
    std::span<std::complex<float>> output,ReleasedBlock& released,bool helper_coverage_contiguous){
    if(have_release_clock_&&now<last_release_clock_)return PopResult::nonmonotonic_release;
    have_release_clock_=true;last_release_clock_=now;
    if(!block_count_)return PopResult::empty;
    const auto& entry=blocks_[block_head_];
    if(now<entry.eligible_nanoseconds)return PopResult::not_ready;
    if(entry.capture.samples_present&&output.size()<entry.capture.sample_count)return PopResult::output_too_small;
    released={};released.capture=entry.capture;released.release_monotonic_nanoseconds=now;
    released.eligible_monotonic_nanoseconds=entry.eligible_nanoseconds;
    released.missing_samples_before=entry.missing_before;released.capture_time_discontinuity=entry.clock_discontinuity;
    released.transmitter_reference_mismatch=entry.reference_mismatch;released.invalid_helper_frontier=frontier>now;
    if(frontier>now)frontier=now; // never count helper samples from the future
    if(frontier>entry.end_monotonic_nanoseconds)
        released.helper_frontier_advance_nanoseconds=frontier-entry.end_monotonic_nanoseconds;
    released.helper_coverage_contiguous=helper_coverage_contiguous&&!released.invalid_helper_frontier;
    released.configured_helper_lookahead_available=released.helper_coverage_contiguous&&
        released.helper_frontier_advance_nanoseconds>=config_.delay_nanoseconds;
    if(entry.capture.samples_present){
        const auto count=entry.capture.sample_count,first=std::min(count,samples_.size()-sample_head_);
        std::copy_n(samples_.begin()+static_cast<std::ptrdiff_t>(sample_head_),first,output.begin());
        std::copy_n(samples_.begin(),count-first,output.begin()+static_cast<std::ptrdiff_t>(first));
        sample_head_=(sample_head_+count)%samples_.size();sample_count_-=count;
    }
    block_head_=(block_head_+1)%blocks_.size();--block_count_;
    return PopResult::released;
}
std::size_t HelperDelayedBuffer::allocated_bytes()const noexcept {
    return samples_.capacity()*sizeof(std::complex<float>)+blocks_.capacity()*sizeof(Entry);
}
bool HelperDelayedBuffer::capture_system_time_at(const CaptureBlock& b,long double sample,
    long double& system_ns,long double& uncertainty_ns) noexcept {
    if(!b.samples_present||!b.sample_count||!std::isfinite(sample)||
       !std::isfinite(b.sample_period_nanoseconds)||b.sample_period_nanoseconds<=0||
       !finite_nonnegative(b.capture_uncertainty_nanoseconds)||
       !finite_nonnegative(b.sample_period_uncertainty_nanoseconds))return false;
    const long double offset=sample-static_cast<long double>(b.first_sample);
    if(offset<0||offset>static_cast<long double>(b.sample_count-1))return false;
    system_ns=static_cast<long double>(b.capture_system_nanoseconds)+offset*b.sample_period_nanoseconds;
    const auto arithmetic_rounding=4*std::numeric_limits<long double>::epsilon()*(
        std::abs(static_cast<long double>(b.capture_system_nanoseconds))+std::abs(offset*b.sample_period_nanoseconds));
    // Fractional resampler coordinates can themselves lose low bits at a large
    // absolute sample index. Conservatively account for their representation
    // and subtraction; the caller still supplies any physical clock error.
    const auto coordinate_rounding=4*std::numeric_limits<long double>::epsilon()*(
        std::abs(sample)+std::abs(static_cast<long double>(b.first_sample)))*b.sample_period_nanoseconds;
    uncertainty_ns=b.capture_uncertainty_nanoseconds+offset*b.sample_period_uncertainty_nanoseconds+
        arithmetic_rounding+coordinate_rounding;
    return std::isfinite(system_ns)&&std::isfinite(uncertainty_ns);
}
}
