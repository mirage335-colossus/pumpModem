#pragma once
#include "datapump/utc_timing.hpp"
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::audio {

// Transport names (including USB), GUI oscillator selections, and a successful
// timestamp fit grant none of these capabilities. A provider must establish
// them for its actual sample clock, including the latency covered by its dates.
struct ProviderTimingCapabilities {
    TimingQuality timestamp_quality=TimingQuality::estimated;
    bool hardware_dated_capture=false;
    bool timestamped_playback=false;
    bool continuous_sample_counters=false;
    bool clock_quality_reports=false;
    bool certified_future_rate=false;
    bool explicit_discontinuity_evidence=false;

    void validate() const {
        if(timestamp_quality!=TimingQuality::unavailable && timestamp_quality!=TimingQuality::estimated &&
           timestamp_quality!=TimingQuality::bounded)
            throw Error("invalid audio-provider timestamp quality");
        if((hardware_dated_capture || timestamped_playback || explicit_discontinuity_evidence ||
            certified_future_rate) && !continuous_sample_counters)
            throw Error("audio-provider timing capabilities require continuous sample counters");
        if(certified_future_rate && !clock_quality_reports)
            throw Error("certified audio-provider rate requires clock-quality reports");
        if(timestamp_quality==TimingQuality::bounded &&
           (!continuous_sample_counters || (!hardware_dated_capture && !timestamped_playback)))
            throw Error("bounded audio-provider dates require hardware sample timing");
        if(timestamp_quality==TimingQuality::unavailable &&
           (hardware_dated_capture || timestamped_playback || certified_future_rate))
            throw Error("audio-provider timing capability has no timestamp evidence");
    }
};

enum class ProviderClockState { unknown, free_running, locked, holdover, unlocked };

// q is UTC seconds per NOMINAL native second, not seconds per frame and not a
// software resampler correction. The provider certifies the complete interval
// (including future wander) only until valid_until_utc. A past rate fit alone
// must not produce this certificate. Holdover needs its own valid certificate.
struct ProviderFutureRateBound {
    double q_lower=1,q_upper=1;
    long double valid_from_utc=0,valid_until_utc=0;
    std::uint64_t counter_generation=0;
    void validate() const {
        if(!std::isfinite(q_lower) || !std::isfinite(q_upper) || q_lower<=0 || q_lower>q_upper ||
           !std::isfinite(valid_from_utc) || !std::isfinite(valid_until_utc) || valid_from_utc<0 ||
           valid_until_utc<valid_from_utc)
            throw Error("invalid audio-provider future-rate certificate");
    }
};

enum class ProviderDiscontinuityKind { dropped, inserted, repeated, displaced, mixed, unknown };
struct ProviderDiscontinuityEvidence {
    std::uint64_t event=0,counter_generation=0;
    // Exclusive native-counter interval affected by this event. Counts refer
    // to actual sample presentation, never callback lateness or ring capacity.
    std::uint64_t first_frame=0,end_frame=0,lost_frames=0,inserted_frames=0,repeated_frames=0;
    std::int64_t displacement_lower_frames=0,displacement_upper_frames=0;
    ProviderDiscontinuityKind kind=ProviderDiscontinuityKind::unknown;
    bool bounded=false;

    void validate() const {
        if(!bounded || !event || end_frame<first_frame ||
           displacement_lower_frames>displacement_upper_frames || kind==ProviderDiscontinuityKind::unknown)
            throw Error("unbounded audio-provider discontinuity");
        const auto extent=end_frame-first_frame;
        if(lost_frames>extent || inserted_frames>extent || repeated_frames>extent)
            throw Error("audio-provider loss counts exceed the affected interval");
        const bool displaced=displacement_lower_frames!=0 || displacement_upper_frames!=0;
        switch(kind) {
        case ProviderDiscontinuityKind::dropped:
            if(!lost_frames || inserted_frames || repeated_frames)throw Error("invalid dropped-sample evidence");
            break;
        case ProviderDiscontinuityKind::inserted:
            if(!inserted_frames || lost_frames || repeated_frames)throw Error("invalid inserted-sample evidence");
            break;
        case ProviderDiscontinuityKind::repeated:
            if(!repeated_frames || lost_frames || inserted_frames)throw Error("invalid repeated-sample evidence");
            break;
        case ProviderDiscontinuityKind::displaced:
            if(!displaced || lost_frames || inserted_frames || repeated_frames)throw Error("invalid displaced-sample evidence");
            break;
        case ProviderDiscontinuityKind::mixed:
            if(!lost_frames && !inserted_frames && !repeated_frames && !displaced)
                throw Error("empty audio-provider discontinuity evidence");
            break;
        default:throw Error("invalid audio-provider discontinuity kind");
        }
    }
};

struct ProviderTimingObservation {
    FrameTimestamp timestamp;
    std::optional<std::uint64_t> sample_counter;
    std::uint64_t counter_generation=0;
    ProviderClockState clock=ProviderClockState::unknown;
    std::optional<ProviderFutureRateBound> future_rate;
    std::optional<ProviderDiscontinuityEvidence> discontinuity;
};

// This validates provider evidence; it does not authorize waveform repair or
// assert that a bounded interruption is harmless to a detector. The caller
// must assess energy, ownership and timing before continuing presentation.
// Failed observations are transactional. Origin/generation are immutable and
// no observation, holdover, hop or event renews an accumulated waveform budget.
class ProviderTimingContract {
public:
    struct EventTotals {
        std::uint64_t events=0,lost_frames=0,inserted_frames=0,repeated_frames=0,affected_frames=0;
        std::int64_t displacement_lower_frames=0,displacement_upper_frames=0;
    };
    ProviderTimingContract(ProviderTimingCapabilities capabilities,std::uint32_t native_rate,bool capture)
        :capabilities_(capabilities),rate_(native_rate),capture_(capture) {
        capabilities_.validate();
        if(!rate_ || rate_>120000000)throw Error("invalid audio-provider native sample rate");
        if(capabilities_.timestamp_quality==TimingQuality::bounded &&
           !(capture_?capabilities_.hardware_dated_capture:capabilities_.timestamped_playback))
            throw Error("audio-provider has no bounded timing capability for this direction");
    }

    void observe(const ProviderTimingObservation& value) {
        const auto& t=value.timestamp;
        if(!std::isfinite(t.frame) || t.frame<0 || !std::isfinite(t.utc_seconds) || t.utc_seconds<0 ||
           !std::isfinite(t.steady_seconds) || !std::isfinite(t.uncertainty_seconds) || t.uncertainty_seconds<0 ||
           !std::isfinite(t.clock_read_uncertainty_seconds) || t.clock_read_uncertainty_seconds<0 ||
           t.quality==TimingQuality::unavailable ||
           (t.quality!=TimingQuality::estimated && t.quality!=TimingQuality::bounded) ||
           capabilities_.timestamp_quality==TimingQuality::unavailable ||
           (t.quality==TimingQuality::bounded && capabilities_.timestamp_quality!=TimingQuality::bounded))
            throw Error("audio-provider observation exceeds its timestamp capability");
        if(capabilities_.continuous_sample_counters) {
            if(!value.sample_counter || t.frame!=static_cast<long double>(*value.sample_counter))
                throw Error("audio-provider date does not identify its continuous sample counter");
        } else if(value.sample_counter)throw Error("audio-provider supplied an undeclared sample counter");
        if(capabilities_.clock_quality_reports) {
            if(value.clock==ProviderClockState::unknown || value.clock==ProviderClockState::unlocked)
                throw Error("audio-provider clock is not qualified or lost lock");
            if(value.clock!=ProviderClockState::free_running && value.clock!=ProviderClockState::locked &&
               value.clock!=ProviderClockState::holdover)throw Error("invalid audio-provider clock state");
        } else if(value.clock!=ProviderClockState::unknown)
            throw Error("audio-provider supplied an undeclared clock-quality report");
        if(value.future_rate && !capabilities_.certified_future_rate)
            throw Error("audio-provider supplied an uncertified future-rate bound");
        auto next_rate=value.future_rate?value.future_rate:future_rate_;
        if(capabilities_.certified_future_rate) {
            if(!next_rate || (value.clock!=ProviderClockState::locked && value.clock!=ProviderClockState::holdover))
                throw Error("audio-provider future rate requires a locked or certified holdover clock");
            next_rate->validate();
            if(next_rate->counter_generation!=value.counter_generation ||
               t.utc_seconds-t.uncertainty_seconds<next_rate->valid_from_utc ||
               t.utc_seconds+t.uncertainty_seconds>next_rate->valid_until_utc)
                throw Error("audio-provider future-rate certificate is expired or has another counter generation");
        }
        if(previous_) {
            const auto& p=*previous_;
            if(value.counter_generation!=origin_->counter_generation || t.continuity!=origin_->timestamp.continuity ||
               t.frame<p.timestamp.frame || t.utc_seconds<p.timestamp.utc_seconds ||
               t.steady_seconds<=p.timestamp.steady_seconds ||
               (capabilities_.continuous_sample_counters && *value.sample_counter<*p.sample_counter))
                throw Error("audio-provider counter generation or continuity changed");
            if((p.clock==ProviderClockState::locked || p.clock==ProviderClockState::holdover) &&
               value.clock==ProviderClockState::free_running)
                throw Error("audio-provider clock lost its timing lock");
            if(future_rate_) {
                // A fresh certificate cannot retroactively close an expired
                // observation gap or excuse a clock step outside the old q.
                if(t.utc_seconds+t.uncertainty_seconds>future_rate_->valid_until_utc)
                    throw Error("audio-provider observation gap exceeded its future-rate certificate");
                const auto elapsed_frames=capabilities_.continuous_sample_counters?
                    static_cast<long double>(*value.sample_counter-*p.sample_counter):t.frame-p.timestamp.frame;
                const auto elapsed=elapsed_frames/rate_;
                const auto actual=t.utc_seconds-p.timestamp.utc_seconds;
                const auto error=t.uncertainty_seconds+p.timestamp.uncertainty_seconds;
                const auto rounding=32*std::numeric_limits<long double>::epsilon()*
                    (std::abs(t.utc_seconds)+std::abs(p.timestamp.utc_seconds)+std::abs(elapsed)+1);
                if(actual+error+rounding<elapsed*future_rate_->q_lower ||
                   actual-error-rounding>elapsed*future_rate_->q_upper)
                    throw Error("audio-provider samples contradict its certified clock-rate interval");
            }
        }
        auto totals=totals_;
        if(value.discontinuity) {
            if(!capabilities_.explicit_discontinuity_evidence || !previous_ || !value.sample_counter)
                throw Error("audio-provider discontinuity lacks established counter evidence");
            const auto& event=*value.discontinuity;event.validate();
            if(event.counter_generation!=value.counter_generation || event.event<=last_event_ ||
               event.first_frame<*previous_->sample_counter || event.end_frame>*value.sample_counter)
                throw Error("audio-provider discontinuity is stale or outside its observed counter interval");
            add(totals.events,1);add(totals.lost_frames,event.lost_frames);
            add(totals.inserted_frames,event.inserted_frames);add(totals.repeated_frames,event.repeated_frames);
            add(totals.affected_frames,event.end_frame-event.first_frame);
            add_signed(totals.displacement_lower_frames,event.displacement_lower_frames);
            add_signed(totals.displacement_upper_frames,event.displacement_upper_frames);
        }
        if(!origin_)origin_=value;
        previous_=value;future_rate_=next_rate;totals_=totals;
        if(value.discontinuity)last_event_=value.discontinuity->event;
    }

    // Request an absolute UTC horizon, including queue/prefetch lookahead.
    // nullopt means this provider has no certified future-rate capability;
    // callers must then retain their explicit estimated-rate policy. Expiry
    // is an error rather than an automatic extension or quality downgrade.
    std::optional<ProviderFutureRateBound> future_rate_interval(long double until_utc) const {
        if(!previous_ || !std::isfinite(until_utc) || until_utc<previous_->timestamp.utc_seconds)
            throw Error("invalid audio-provider timing prediction horizon");
        if(!capabilities_.certified_future_rate)return std::nullopt;
        if(!future_rate_ || until_utc>future_rate_->valid_until_utc)
            throw Error("audio-provider future-rate certificate expired before the prediction horizon");
        return future_rate_;
    }
    const std::optional<ProviderTimingObservation>& origin() const noexcept {return origin_;}
    const std::optional<ProviderTimingObservation>& latest() const noexcept {return previous_;}
    const EventTotals& events() const noexcept {return totals_;}
    const ProviderTimingCapabilities& capabilities() const noexcept {return capabilities_;}

private:
    static void add(std::uint64_t& total,std::uint64_t value) {
        if(value>std::numeric_limits<std::uint64_t>::max()-total)
            throw Error("audio-provider event accounting overflow");
        total+=value;
    }
    static void add_signed(std::int64_t& total,std::int64_t value) {
        if((value>0 && total>std::numeric_limits<std::int64_t>::max()-value) ||
           (value<0 && total<std::numeric_limits<std::int64_t>::min()-value))
            throw Error("audio-provider displacement accounting overflow");
        total+=value;
    }
    ProviderTimingCapabilities capabilities_;
    std::uint32_t rate_;
    bool capture_;
    std::optional<ProviderTimingObservation> origin_,previous_;
    std::optional<ProviderFutureRateBound> future_rate_;
    EventTotals totals_;
    std::uint64_t last_event_=0;
};

} // namespace datapump::audio
