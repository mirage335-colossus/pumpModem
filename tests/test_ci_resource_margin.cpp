#include "cpu_work_budget.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

template<class Action>
void rejects(Action action,const char* message) {
    try { action(); }
    catch(const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}

void resource_margin_boundaries() {
    using namespace datapump::testing;
    check(resource_limit(25.0,true)==31.25,"CI wall-time allowance rounded down");
    check(within_resource_limit(31.25,25.0,true),"exact CI wall-time limit was rejected");
    check(!within_resource_limit(std::nextafter(31.25,std::numeric_limits<double>::infinity()),25.0,true),
          "wall time above CI limit was accepted");
    check(resource_limit(25.0,false)==25.0,"local wall-time budget changed");
    check(!within_resource_limit(25.000001,25.0,false),"local wall-time overrun received CI margin");

    check(resource_limit(std::size_t{100},true)==std::size_t{125},"byte footprint did not receive exact 25 percent margin");
    check(resource_limit(std::size_t{1},true)==std::size_t{2},"fractional byte allowance rounded down");
    check(resource_limit(std::size_t{100},true,std::size_t{300})==std::size_t{300},
          "an established larger resource allowance was reduced");
    check(within_resource_limit(std::size_t{125},std::size_t{100},true),"exact byte limit was rejected");
    check(!within_resource_limit(std::size_t{126},std::size_t{100},true),"byte footprint above CI limit was accepted");
    const auto largest=std::numeric_limits<std::size_t>::max();
    check(resource_limit(largest,true)==largest&&within_resource_limit(largest,largest,true),
          "SIZE_MAX resource bound wrapped or lost integer precision");

    std::ostringstream warning;
    check(meets_resource_budget(26.0,25.0,"wall benchmark","s",true,warning),
          "in-margin CI resource benchmark failed");
    const auto report=warning.str();
    check(report.find("value=26 s, nominal=25 s, limit=31.25 s, status=within-limit")!=std::string::npos&&
          report.find("TEST_WORKLOAD_BUDGET:")!=std::string::npos&&
          report.find("TEST_WORKLOAD_BUDGET:")==report.rfind("TEST_WORKLOAD_BUDGET:"),
          "CI margin did not emit one bounded metric and warning");

    std::ostringstream outside;
    check(!report_resource_budget(32.0,25.0,31.25,"wall benchmark","s",true,outside),
          "over-limit benchmark passed");
    check(outside.str().find("TEST_RESOURCE_METRIC:")!=std::string::npos&&
          outside.str().find("TEST_WORKLOAD_BUDGET:")==std::string::npos,
          "over-limit benchmark was reported as an accepted workload warning");

    std::ostringstream local;
    check(!meets_resource_budget(26.0,25.0,"wall benchmark","s",false,local)&&
          local.str().find("TEST_WORKLOAD_BUDGET:")==std::string::npos,
          "local strict budget emitted a CI allowance warning");

    rejects([]{(void)resource_limit(0.0,true);},"zero floating nominal bound accepted");
    rejects([]{(void)resource_limit(std::numeric_limits<double>::infinity(),true);},"infinite nominal bound accepted");
    rejects([]{(void)resource_limit(std::numeric_limits<double>::max(),true);},"overflowing margin bound accepted");
    rejects([]{(void)within_resource_limit(std::numeric_limits<double>::quiet_NaN(),25.0,true);},
            "NaN observation accepted");
    rejects([]{(void)within_resource_limit(std::numeric_limits<double>::infinity(),25.0,true);},
            "infinite observation accepted");
    rejects([]{(void)report_resource_budget(1.0,25.0,std::numeric_limits<double>::infinity(),"stage","s",true);},
            "infinite report bound accepted");
}

void cpu_work_budget_boundaries() {
    using Budget=datapump::testing::CpuWorkBudget;
    const auto origin=Budget::Clock::time_point{};
    std::ostringstream local_log,ci_log,instrumented_log;
    Budget local(std::chrono::seconds(25),"ordinary",false,origin,local_log,false);
    check(local.pending(origin+std::chrono::milliseconds(24999))&&
          !local.pending(origin+std::chrono::seconds(25))&&local_log.str().empty(),
          "local CPU-functional timeout changed or emitted a warning");

    Budget ci(std::chrono::seconds(25),"ordinary CI",false,origin,ci_log,true);
    check(ci.pending(origin+std::chrono::milliseconds(31249))&&
          !ci.pending(origin+std::chrono::milliseconds(31250)),
          "ordinary CI CPU-functional allowance did not preserve its fractional second");
    (void)ci.pending(origin+std::chrono::seconds(32));
    const auto ci_warning=ci_log.str();
    check(ci_warning.find("CI limit 31.25s")!=std::string::npos&&
          ci_warning.find("TEST_WORKLOAD_BUDGET:")==ci_warning.rfind("TEST_WORKLOAD_BUDGET:"),
          "CI CPU-functional budget did not warn exactly once with its effective limit");

    Budget instrumented(std::chrono::seconds(30),"instrumented",true,origin,instrumented_log,true);
    check(instrumented.pending(origin+std::chrono::seconds(30)),
          "instrumented allowance ended at its nominal budget");
    check(instrumented.pending(origin+std::chrono::seconds(90))==false,
          "instrumented threefold limit was multiplied by the CI margin");
    check(instrumented_log.str().find("instrumented limit 90s")!=std::string::npos,
          "CI instrumentation did not retain the established threefold allowance");
}
}

int main() {
    try {
        resource_margin_boundaries();
        cpu_work_budget_boundaries();
        std::cout<<"CI resource margin boundaries passed\n";
    } catch(const std::exception& error) {
        std::cerr<<"CI resource margin boundary failure: "<<error.what()<<'\n';
        return 1;
    }
}
