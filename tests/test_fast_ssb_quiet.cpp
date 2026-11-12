#include "fast/ssb_quiet.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace datapump::fast::detail;

namespace {
constexpr std::uint32_t rate=48000;
constexpr std::size_t stride=4;
constexpr std::uint64_t origin=std::uint64_t{1}<<40;

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

void check_complete_observations() {
    SsbQuietRun quiet(rate,stride);
    // Power is deliberately large. The supplied absence decision represents
    // an independent, fully executed public-waveform test.
    std::vector<std::complex<double>> samples(rate*6/stride,{2.,-3.});
    const SsbAcquisitionView view{origin,stride,samples};
    const auto last=origin+samples.size()*stride;
    check(!quiet.observe(view,{origin,last-stride},SsbPublicPresence::absent) &&
          quiet.elapsed_input_ticks()==rate*6-stride && !quiet.completion_tick(),
          "Fast SSB completed before six seconds of observed groups");
    auto truncated=view;
    truncated.samples=truncated.samples.first(truncated.samples.size()-1);
    check(!quiet.observe(truncated,{last-stride,last},SsbPublicPresence::absent) &&
          quiet.elapsed_input_ticks()==0,
          "Fast SSB counted a group with an unobserved final center");
    check(!quiet.observe(view,{origin,last-stride},SsbPublicPresence::absent),
          "Fast SSB completed before the final actual group");
    check(quiet.observe(view,{last-stride,last},SsbPublicPresence::absent) &&
          quiet.completion_tick()==last,
          "Fast SSB failed to count six contiguous seconds of independent absence");
}

void check_indeterminate_and_power() {
    SsbQuietRun quiet(rate,stride);
    std::vector<std::complex<double>> samples(640,{.1,.1});
    const SsbAcquisitionView view{origin,stride,samples};
    const auto split=origin+samples.size()/2*stride;
    const SsbCenterRange first{origin,split};
    const SsbCenterRange second{split,origin+samples.size()*stride};
    check(!quiet.observe(view,first,SsbPublicPresence::no_detection) &&
          quiet.elapsed_input_ticks()==0,"Fast SSB counted uncalibrated no-detection");
    check(!quiet.observe(view,first,SsbPublicPresence::no_detection,.021) &&
          quiet.elapsed_input_ticks()==split-origin,"Fast SSB discarded calibrated low observed power");
    check(!quiet.observe(view,second,SsbPublicPresence::unscored,.021) &&
          quiet.elapsed_input_ticks()==0,"Fast SSB counted unscored work as physical absence");
    check(!quiet.observe(view,first,SsbPublicPresence::no_detection,.019) &&
          quiet.elapsed_input_ticks()==0,"Fast SSB ignored its independent calibrated power ceiling");
    check(!quiet.observe(view,first,SsbPublicPresence::absent) &&
          !quiet.observe(view,{split+stride,second.past_last},SsbPublicPresence::absent) &&
          quiet.elapsed_input_ticks()==second.past_last-split-stride,
          "Fast SSB bridged an unobserved gap");
    check(!quiet.observe(view,first,SsbPublicPresence::present) &&
          quiet.elapsed_input_ticks()==0,"Fast SSB continued quiet after public waveform presence");
}

void check_pooled_observability() {
    std::array<SsbQuietGroupProjection,ssb_quiet_minimum_groups> groups;
    const auto configure=[](SsbQuietGroupProjection& group,double retained,double quality) {
        group={160,1.,retained,1.,std::sqrt(retained*quality)};
    };
    for(auto& group:groups)configure(group,.10,.02);
    configure(groups.back(),.02,.02);
    SsbQuietUnitDiagnostics diagnostics;
    check(classify_ssb_quiet_unit(groups,false,{},&diagnostics)==SsbPublicPresence::absent &&
          diagnostics.observable && diagnostics.informative==groups.size()-1,
          "A weak constituent erased valid pooled public observability");

    configure(groups.back(),.02,.91);
    check(classify_ssb_quiet_unit(groups,false,{},&diagnostics)==SsbPublicPresence::present &&
          diagnostics.groups[groups.size()-1].strong,"Pooled quiet ignored a surviving public fragment");

    configure(groups.back(),0.,0.);
    check(classify_ssb_quiet_unit(groups,false,{},&diagnostics)==SsbPublicPresence::no_detection &&
          !diagnostics.observable,"Pooled quiet claimed absent for an unrepresented constituent");

    for(auto& group:groups)configure(group,.02,.02);
    check(classify_ssb_quiet_unit(groups,false,{},&diagnostics)==SsbPublicPresence::no_detection &&
          !diagnostics.observable,"Pooled quiet accepted wholly weak retained pilot energy");
}

void check_fixed_units() {
    for(const auto count:{64U,66U}) {
        const auto units=ssb_quiet_units(count);
        check(units.size()==8,"Dense-pilot SSB quiet partition changed the observation cadence");
        std::size_t next=0;
        for(const auto unit:units) {
            check(unit.first==next && unit.end-unit.first>=ssb_quiet_minimum_groups &&
                  unit.end-unit.first<=ssb_quiet_maximum_groups,"Fast SSB quiet partition left a gap");
            next=unit.end;
        }
        check(next==count && units.back().end-units.back().first==count-56,
              "Fast SSB quiet partition lost the complete trailing public groups");
    }
}
}

int main() {
    try {
        check_complete_observations();
        check_indeterminate_and_power();
        check_pooled_observability();
        check_fixed_units();
        std::cout<<"Fast SSB physical quiet uses complete, contiguous scored observations\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
