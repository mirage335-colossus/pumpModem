#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace datapump::testing {

inline bool is_ci_environment() noexcept {
    const auto* actions=std::getenv("GITHUB_ACTIONS");
    const auto* ci=std::getenv("CI");
    return (actions&&std::string_view(actions)=="true") ||
           (ci&&std::string_view(ci)=="true");
}

inline double resource_limit(double nominal,bool ci,double established_limit=0.0) {
    if(!std::isfinite(nominal)||nominal<=0.0 || !std::isfinite(established_limit)||established_limit<0.0)
        throw std::invalid_argument("resource bounds must be finite and nominal must be positive");
    const double ci_limit=ci?nominal*1.25:nominal;
    if(!std::isfinite(ci_limit))throw std::invalid_argument("resource limit overflow");
    return std::max({nominal,ci_limit,established_limit});
}

// Integer observations such as retained bytes round the fractional byte up.
// Quotient arithmetic avoids imprecise double conversion and overflow at SIZE_MAX.
inline std::size_t resource_limit(std::size_t nominal,bool ci,std::size_t established_limit=0) {
    if(nominal==0)throw std::invalid_argument("resource bound must be positive");
    const auto extra=ci?nominal/4+(nominal%4?1:0):std::size_t{0};
    const auto max=std::numeric_limits<std::size_t>::max();
    const auto ci_limit=extra>max-nominal?max:nominal+extra;
    return std::max({nominal,ci_limit,established_limit});
}

inline bool within_resource_limit(double observed,double nominal,bool ci,
                                  double established_limit=0.0) {
    if(!std::isfinite(observed)||observed<0.0)throw std::invalid_argument("resource observation must be finite and nonnegative");
    return observed<=resource_limit(nominal,ci,established_limit);
}

inline bool within_resource_limit(std::size_t observed,std::size_t nominal,bool ci,
                                  std::size_t established_limit=0) {
    return observed<=resource_limit(nominal,ci,established_limit);
}

inline bool report_resource_budget(double observed,double nominal,double effective_limit,
                                   std::string_view stage,std::string_view units,bool ci,
                                   std::ostream& output=std::cerr) {
    if(!std::isfinite(observed)||observed<0.0||!std::isfinite(nominal)||nominal<=0.0||
       !std::isfinite(effective_limit)||effective_limit<nominal)
        throw std::invalid_argument("resource metrics and limits must be finite and in range");
    const bool within=observed<=effective_limit;
    output<<"TEST_RESOURCE_METRIC: "<<stage<<": value="<<observed<<' '<<units
          <<", nominal="<<nominal<<' '<<units<<", limit="<<effective_limit<<' '<<units
          <<", status="<<(within?"within-limit":"exceeded-limit")<<'\n';
    if(ci&&observed>nominal&&within)
        output<<"TEST_WORKLOAD_BUDGET: "<<stage<<": exceeded nominal resource budget; CI limit "
              <<effective_limit<<' '<<units<<" (full completion remains required).\n";
    return within;
}

inline bool report_resource_budget(std::size_t observed,std::size_t nominal,std::size_t effective_limit,
                                   std::string_view stage,std::string_view units,bool ci,
                                   std::ostream& output=std::cerr) {
    if(nominal==0||effective_limit<nominal)
        throw std::invalid_argument("resource limits must be positive and ordered");
    const bool within=observed<=effective_limit;
    output<<"TEST_RESOURCE_METRIC: "<<stage<<": value="<<observed<<' '<<units
          <<", nominal="<<nominal<<' '<<units<<", limit="<<effective_limit<<' '<<units
          <<", status="<<(within?"within-limit":"exceeded-limit")<<'\n';
    if(ci&&observed>nominal&&within)
        output<<"TEST_WORKLOAD_BUDGET: "<<stage<<": exceeded nominal resource budget; CI limit "
              <<effective_limit<<' '<<units<<" (full completion remains required).\n";
    return within;
}

inline bool meets_resource_budget(double observed,double nominal,std::string_view stage,
                                  std::string_view units,bool ci=is_ci_environment(),
                                  std::ostream& output=std::cerr,double established_limit=0.0) {
    const auto effective_limit=resource_limit(nominal,ci,established_limit);
    return report_resource_budget(observed,nominal,effective_limit,stage,units,ci,output);
}

inline bool meets_resource_budget(std::size_t observed,std::size_t nominal,std::string_view stage,
                                  std::string_view units,bool ci=is_ci_environment(),
                                  std::ostream& output=std::cerr,std::size_t established_limit=0) {
    const auto effective_limit=resource_limit(nominal,ci,established_limit);
    return report_resource_budget(observed,nominal,effective_limit,stage,units,ci,output);
}

// Existing benchmark predicates use an exclusive nominal bound locally. CI
// retains that target for warnings and accepts the full 25% allowance, including
// its endpoint. Do not use this for allocator grants or functional deadlines.
inline bool benchmark_below(double observed,double nominal,std::string_view stage,
                            std::string_view units) {
    if(!is_ci_environment()) {
        (void)within_resource_limit(observed,nominal,false);
        return observed<nominal;
    }
    return meets_resource_budget(observed,nominal,stage,units);
}

inline bool benchmark_below(std::size_t observed,std::size_t nominal,std::string_view stage,
                            std::string_view units) {
    if(!is_ci_environment()) {
        (void)resource_limit(nominal,false);
        return observed<nominal;
    }
    return meets_resource_budget(observed,nominal,stage,units);
}

inline bool benchmark_at_most(std::size_t observed,std::size_t nominal,std::string_view stage,
                              std::string_view units) {
    if(!is_ci_environment())return within_resource_limit(observed,nominal,false);
    return meets_resource_budget(observed,nominal,stage,units);
}

} // namespace datapump::testing
