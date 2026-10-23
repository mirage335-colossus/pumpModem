#pragma once
#include <chrono>
#include <iostream>
#include <string_view>
#include "resource_test_margin.hpp"

namespace datapump::testing {
#if defined(DATAPUMP_INSTRUMENTED_TEST)
inline constexpr bool instrumented_test=true;
#else
inline constexpr bool instrumented_test=false;
#endif

// Only for CPU-functional fixtures with independently controlled media clocks.
// Never use this allowance for real-time throughput, cancellation or physical
// absence requirements. The larger budget still requires complete assertions.
class CpuWorkBudget {
public:
    using Clock=std::chrono::steady_clock;
    CpuWorkBudget(std::chrono::seconds base,std::string_view stage,
                  bool instrumented=instrumented_test,Clock::time_point started=Clock::now(),
                  std::ostream& warnings=std::cerr,bool ci=is_ci_environment())
        : base_(base),limit_seconds_(resource_limit(static_cast<double>(base.count()),ci,
                                                   static_cast<double>(base.count())*(instrumented?3.0:1.0))),
          stage_(stage),started_(started),warnings_(warnings),instrumented_(instrumented),ci_(ci) {}
    bool pending(Clock::time_point now=Clock::now()) {
        const auto elapsed=std::chrono::duration<double>(now-started_).count();
        if((instrumented_||ci_)&&!warned_&&elapsed>=base_.count()&&elapsed<limit_seconds_) {
            // The CI runner extracts this marker from passing JUnit output too.
            warnings_<<"TEST_WORKLOAD_BUDGET: "<<stage_<<": exceeded "<<base_.count()
                <<"s normal budget; ";
            if(instrumented_&&limit_seconds_==static_cast<double>(base_.count())*3.0)
                warnings_<<"instrumented limit "<<limit_seconds_<<"s";
            else
                warnings_<<"CI limit "<<limit_seconds_<<"s";
            warnings_<<" (full completion remains required).\n";
            warned_=true;
        }
        return elapsed<limit_seconds_;
    }
private:
    std::chrono::seconds base_;
    double limit_seconds_;
    std::string_view stage_;
    Clock::time_point started_;
    std::ostream& warnings_;
    bool instrumented_,ci_,warned_=false;
};
}
