#pragma once
#include <chrono>
#include <iostream>
#include <string_view>

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
                  std::ostream& warnings=std::cerr)
        : base_(base),limit_(base*(instrumented?3:1)),stage_(stage),started_(started),
          warnings_(warnings),instrumented_(instrumented) {}
    bool pending(Clock::time_point now=Clock::now()) {
        if(instrumented_&&!warned_&&now>=started_+base_) {
            // The CI runner extracts this marker from passing JUnit output too.
            warnings_<<"TEST_WORKLOAD_BUDGET: "<<stage_<<": exceeded "<<base_.count()
                <<"s normal budget; instrumented limit "<<limit_.count()
                <<"s (full completion remains required).\n";
            warned_=true;
        }
        return now<started_+limit_;
    }
private:
    std::chrono::seconds base_,limit_;
    std::string_view stage_;
    Clock::time_point started_;
    std::ostream& warnings_;
    bool instrumented_,warned_=false;
};
}
