#include "datapump/helper_stream_conditioner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
double wrapped(long double phase) noexcept {
    return static_cast<double>(std::remainder(phase,static_cast<long double>(tau)));
}
}

void HelperStreamConditioner::validate(Correction c) {
    if(!std::isfinite(c.audio_scale) || c.audio_scale<=0 ||
       !std::isfinite(c.common_frequency_hz) ||
       !std::isfinite(c.maximum_slew_per_second) || c.maximum_slew_per_second<=0 ||
       c.maximum_slew_per_second>.001 || std::abs(1/c.audio_scale-1)>.001)
        throw Error("invalid Helper stream correction");
}
HelperStreamConditioner::HelperStreamConditioner(Config c,Correction correction)
    :config_(c),correction_(correction),real_(c.sample_rate,c.sample_rate,true),
     imaginary_(c.sample_rate,c.sample_rate,true),next_input_sample_(c.first_input_sample),
     next_output_sample_(c.first_output_sample) {
    validate(correction);
    if(!std::isfinite(c.clock_time_at_first_input_seconds) ||
       !std::isfinite(c.fractional_input_sample) || c.fractional_input_sample<0 ||
       c.fractional_input_sample>=1 || !std::isfinite(c.maximum_clock_error_seconds) ||
       c.maximum_clock_error_seconds<0 || std::abs(correction.common_frequency_hz)>=c.sample_rate/2.)
        throw Error("invalid Helper stream origin");
    clock_history_[0]={c.first_input_sample,c.clock_time_at_first_input_seconds,correction.audio_scale};
    clock_history_size_=1;
    const double rate=1/correction.audio_scale-1;
    real_.initialize_timing(c.fractional_input_sample,rate);
    imaginary_.initialize_timing(c.fractional_input_sample,rate);
    real_.set_output_transform([this](float value,long double seconds,std::uint32_t rate_hz) {
        if(real_dates_>=block_capacity || rate_hz!=config_.sample_rate)
            throw Error("Helper stream real date overflow");
        source_coordinates_[real_dates_++]=static_cast<long double>(config_.first_input_sample)+seconds*rate_hz;
        return value;
    });
    imaginary_.set_output_transform([this](float value,long double seconds,std::uint32_t rate_hz) {
        if(imaginary_dates_>=real_dates_ || rate_hz!=config_.sample_rate)
            throw Error("Helper stream imaginary date overflow");
        const long double date=static_cast<long double>(config_.first_input_sample)+seconds*rate_hz;
        if(std::abs(date-source_coordinates_[imaginary_dates_])>1e-8L)
            throw Error("Helper stream complex time maps differ");
        ++imaginary_dates_;
        return value;
    });
}

void HelperStreamConditioner::update(Correction next) {
    validate(next);
    if(std::abs(next.common_frequency_hz)>=config_.sample_rate/2.)
        throw Error("Helper stream mixer is outside capture");
    const auto clock=clock_time_at_input(next_input_sample_);
    prune_clock_history();
    const bool replace=clock_history_[clock_history_size_-1].first==next_input_sample_;
    if(!replace && clock_history_size_==clock_history_capacity)
        throw Error("Helper stream clock history is full before output advances");
    real_.set_rate_correction(1/next.audio_scale-1,next.maximum_slew_per_second);
    imaginary_.set_rate_correction(1/next.audio_scale-1,next.maximum_slew_per_second);
    const auto index=replace?clock_history_size_-1:clock_history_size_++;
    clock_history_[index]={next_input_sample_,clock,next.audio_scale};
    if(next.audio_scale!=correction_.audio_scale)clear_phase();
    correction_=next;
}

std::optional<long double> HelperStreamConditioner::clock_time(long double sample) const noexcept {
    if(!std::isfinite(sample) || !clock_history_size_ || sample<clock_history_[0].first)return std::nullopt;
    std::size_t index=clock_history_size_-1;
    while(index && sample<clock_history_[index].first)--index;
    const auto& segment=clock_history_[index];
    const auto seconds=segment.seconds+(sample-segment.first)*segment.scale/config_.sample_rate;
    if(!std::isfinite(seconds))return std::nullopt;
    return seconds;
}
long double HelperStreamConditioner::clock_time_at_input(long double sample) const {
    const auto clock=clock_time(sample);
    if(!clock)throw Error("Helper stream clock query precedes retained model history");
    return *clock;
}
void HelperStreamConditioner::prune_clock_history() noexcept {
    const long double keep=next_source_sample()-std::ceil(lookahead_seconds()*config_.sample_rate);
    while(clock_history_size_>1 && clock_history_[1].first<=keep) {
        for(std::size_t i=1;i<clock_history_size_;++i)clock_history_[i-1]=clock_history_[i];
        --clock_history_size_;
    }
}
std::optional<long double> HelperStreamConditioner::timing_error_at(long double source,std::uint64_t output) const noexcept {
    const auto clock=clock_time(source);
    if(!clock)return std::nullopt;
    return *clock-static_cast<long double>(output)/config_.sample_rate;
}
bool HelperStreamConditioner::timing_supported_at(long double source,std::uint64_t output) const noexcept {
    const auto error=timing_error_at(source,output);
    return error && std::abs(*error)<=config_.maximum_clock_error_seconds;
}

bool HelperStreamConditioner::set_phase(const LocalPhase& reference) noexcept {
    if(!std::isfinite(reference.anchor_input_sample) ||
       !std::isfinite(reference.correction_phase_radians) ||
       reference.support_first_sample>=reference.support_end_sample ||
       reference.support_first_sample<config_.first_input_sample ||
       reference.anchor_input_sample<reference.support_first_sample ||
       reference.anchor_input_sample>=reference.support_end_sample ||
       reference.captured_end_sample<reference.support_end_sample ||
       next_input_sample_>=reference.support_end_sample) return false;
    mixer_.phase_reference=reference;mixer_.pending=true;
    return true;
}
void HelperStreamConditioner::clear_phase() noexcept {mixer_.pending=false;mixer_.active=false;}
void HelperStreamConditioner::clear_phase_and_multiplier() noexcept {
    clear_phase();mixer_.phasor={1,0};mixer_.phase=0;
}

void HelperStreamConditioner::advance(MixerState& state,std::uint64_t sample) const noexcept {
    if(state.pending && sample>=state.phase_reference.support_first_sample) {
        const auto& reference=state.phase_reference;
        if(sample<reference.support_end_sample) {
            const double phase=wrapped(reference.correction_phase_radians-
                static_cast<long double>(tau)*correction_.common_frequency_hz*
                (static_cast<long double>(sample)-reference.anchor_input_sample)/config_.sample_rate);
            if(config_.use_recursive_oscillator)state.phasor=std::polar(1.,phase);
            else state.phase=phase;
            // Earlier samples may already be mixed and retained in the
            // resampler's lookahead/history. A late/replacement anchor gives
            // no authority over them, even if its observation began earlier.
            state.active=true;state.active_first=sample;state.active_end=reference.support_end_sample;
        }
        state.pending=false;
    }
}

bool HelperStreamConditioner::phase_supported_at(long double source) const noexcept {
    // Account for every interpolation dependency, not merely its center.
    const long double halo=std::ceil(real_.source_lookahead_seconds()*config_.sample_rate);
    return mixer_.active && std::isfinite(source) &&
        source-halo>=mixer_.active_first && source+halo<mixer_.active_end;
}

HelperStreamConditioner::Progress HelperStreamConditioner::process(
    std::span<const std::complex<float>> input,std::span<std::complex<float>> output,
    std::uint64_t first,bool end,std::span<long double> dates) {
    if(first!=next_input_sample_)throw Error("Helper stream input is not contiguous");
    if(!dates.empty() && dates.size()<output.size())throw Error("Helper stream date span is too short");
    const auto available=std::min(input.size(),block_capacity),capacity=std::min(output.size(),block_capacity);
    if(available>std::numeric_limits<std::uint64_t>::max()-next_input_sample_)
        throw Error("Helper stream input counter overflow");
    MixerState state=mixer_;
    const double increment=-tau*correction_.common_frequency_hz/config_.sample_rate;
    if(config_.use_recursive_oscillator){
        // One phasor per contiguous mixer state. A fresh RF-hop anchor resets
        // it; update() changes the increment without resetting phase. Phasor
        // state is snapshotted for speculative input, so only the consumed
        // prefix commits. The re-normalization cadence is absolute-indexed,
        // independent of chunking, and limits magnitude rather than phase
        // drift. Long-run phase error remains a numerical measurement.
        const auto step=std::polar(1.,increment);
        for(std::size_t i=0;i<available;++i) {
            const auto z=input[i];
            if(!std::isfinite(z.real()) || !std::isfinite(z.imag()))throw Error("nonfinite Helper stream input");
            advance(state,next_input_sample_+i);
            const auto mixed=std::complex<double>(z)*state.phasor;
            real_input_[i]=static_cast<float>(mixed.real());imaginary_input_[i]=static_cast<float>(mixed.imag());
            state.phasor*=step;
            if(((next_input_sample_+i)&255U)==255U)state.phasor/=std::abs(state.phasor);
            after_[i]=state;
        }
    }else{
        // Selectable original scalar path for matched comparisons.
        for(std::size_t i=0;i<available;++i) {
            const auto z=input[i];
            if(!std::isfinite(z.real()) || !std::isfinite(z.imag()))throw Error("nonfinite Helper stream input");
            advance(state,next_input_sample_+i);
            const auto mixed=std::complex<double>(z)*std::polar(1.,state.phase);
            real_input_[i]=static_cast<float>(mixed.real());imaginary_input_[i]=static_cast<float>(mixed.imag());
            state.phase=wrapped(state.phase+increment);after_[i]=state;
        }
    }
    real_dates_=imaginary_dates_=0;
    const bool local_end=end && available==input.size();
    const auto r=real_.process(std::span<const float>(real_input_.data(),available),
        std::span<float>(real_output_.data(),capacity),local_end);
    const auto q=imaginary_.process(std::span<const float>(imaginary_input_.data(),available),
        std::span<float>(imaginary_output_.data(),capacity),local_end);
    if(r.consumed!=q.consumed || r.produced!=q.produced || real_dates_!=r.produced || imaginary_dates_!=r.produced)
        throw Error("Helper stream complex resamplers diverged");
    if(r.consumed)mixer_=after_[r.consumed-1];
    if(r.produced>std::numeric_limits<std::uint64_t>::max()-next_output_sample_)
        throw Error("Helper stream output counter overflow");
    Progress result;
    result.consumed=r.consumed;result.produced=r.produced;result.output_first_sample=next_output_sample_;
    result.phase_supported=r.produced!=0;
    result.timing_supported=result.timing_available=r.produced!=0;
    for(std::size_t i=0;i<r.produced;++i) {
        output[i]={real_output_[i],imaginary_output_[i]};
        if(!dates.empty())dates[i]=source_coordinates_[i];
        result.phase_supported &= phase_supported_at(source_coordinates_[i]);
        const auto error=timing_error_at(source_coordinates_[i],next_output_sample_+i);
        if(!error)result.timing_supported=result.timing_available=false;
        else {
            result.maximum_timing_error_seconds=std::max(result.maximum_timing_error_seconds,
                static_cast<double>(std::abs(*error)));
            result.timing_supported &= std::abs(*error)<=config_.maximum_clock_error_seconds;
        }
    }
    result.correction_supported=result.phase_supported && result.timing_supported;
    if(!result.timing_available && r.produced)
        result.maximum_timing_error_seconds=std::numeric_limits<double>::quiet_NaN();
    if(r.produced){result.source_first_sample=source_coordinates_[0];result.source_last_sample=source_coordinates_[r.produced-1];}
    next_input_sample_+=r.consumed;next_output_sample_+=r.produced;
    return result;
}

long double HelperStreamConditioner::next_source_sample() const noexcept {
    return static_cast<long double>(config_.first_input_sample)+real_.source_seconds()*config_.sample_rate;
}
bool HelperStreamConditioner::finished() const noexcept {return real_.finished() && imaginary_.finished();}
double HelperStreamConditioner::lookahead_seconds() const noexcept {return real_.source_lookahead_seconds();}
std::size_t HelperStreamConditioner::workspace_bytes() const noexcept {
    return sizeof(*this)+real_.workspace_bytes()-sizeof(real_)+imaginary_.workspace_bytes()-sizeof(imaginary_);
}
}
