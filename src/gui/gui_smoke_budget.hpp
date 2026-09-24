#pragma once
#include "datapump/types.hpp"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

namespace datapump::gui {
inline constexpr int smoke_budget_exit_code=75;
inline constexpr const char* smoke_budget_marker="INCOMPLETE GUI_SMOKE_BUDGET: ";

namespace smoke_detail {
// Only observable sampled-transmission work can distinguish an exhausted
// overall workload allowance from a stalled workflow. Polls, status changes,
// idle audio and elapsed computation time are deliberately not observations.
struct SampledWork {
    std::uint64_t transmission=0,samples=0;
    double fraction=0,media_seconds=0;
    bool simulation=false,transmitting=false,noise=false,replay=false,tail=false,finished=true,failed=false;
    bool eligible() const {
        return transmission&&simulation&&transmitting&&!noise&&!replay&&!finished&&!failed&&
            std::isfinite(fraction)&&fraction>=0&&fraction<=1&&
            std::isfinite(media_seconds)&&media_seconds>=0;
    }
};
class SampledWorkProgress {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr auto recent_window=std::chrono::seconds(30);
    void observe(const SampledWork& work,Clock::time_point now) {
        if(work.transmission&&work.transmission!=transmission_) {
            transmission_=work.transmission;disqualified_=false;previous_.reset();advanced_.reset();
        }
        if(work.failed||!std::isfinite(work.fraction)||work.fraction<0||work.fraction>1||
           !std::isfinite(work.media_seconds)||work.media_seconds<0)disqualified_=true;
        if(disqualified_||!work.eligible()) {previous_.reset();advanced_.reset();return;}
        if(!previous_) {
            previous_=work;advanced_.reset();return;
        }
        // Regressing counters do not establish healthy forward computation.
        if(work.fraction<previous_->fraction||work.media_seconds<previous_->media_seconds||
           work.samples<previous_->samples||(previous_->tail&&!work.tail)) {
            disqualified_=true;previous_.reset();advanced_.reset();return;
        }
        if(work.fraction>previous_->fraction||work.media_seconds>previous_->media_seconds||
           (work.tail&&previous_->tail&&work.samples>previous_->samples))advanced_=now;
        previous_=work;
    }
    bool advancing(Clock::time_point now) const {
        return previous_&&advanced_&&now>=*advanced_&&now-*advanced_<=recent_window;
    }
    std::optional<double> progress_age(Clock::time_point now) const {
        if(!advanced_)return std::nullopt;
        return std::chrono::duration<double>(now-*advanced_).count();
    }
private:
    std::uint64_t transmission_=0;
    bool disqualified_=false;
    std::optional<SampledWork> previous_;
    std::optional<Clock::time_point> advanced_;
};
}

// This remains a nonzero result on every ordinary invocation. Only the
// explicitly scoped CI wrapper may classify this exact typed result as an
// incomplete workload check; it is never a completed smoke pass.
class SmokeBudgetExhausted final : public Error {
public:
    SmokeBudgetExhausted(int phase,double elapsed,double budget,const smoke_detail::SampledWork& work,double progress_age)
        :Error(describe(phase,elapsed,budget,work,progress_age)) {}
private:
    static std::string describe(int phase,double elapsed,double budget,const smoke_detail::SampledWork& work,double progress_age) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out<<std::fixed<<std::setprecision(6)
           <<"phase="<<phase<<" elapsed="<<elapsed<<" budget="<<budget
           <<" tx_id="<<work.transmission<<" fraction="<<work.fraction
           <<" media_seconds="<<work.media_seconds<<" samples="<<work.samples
           <<" tail="<<work.tail<<" progress_age="<<progress_age<<" result=incomplete";
        return out.str();
    }
};
}
