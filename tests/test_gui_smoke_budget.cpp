#include "gui_smoke_budget.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace datapump::gui;
using smoke_detail::SampledWork;
using smoke_detail::SampledWorkProgress;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
auto at(std::chrono::milliseconds value) {return SampledWorkProgress::Clock::time_point{}+value;}
using namespace std::chrono_literals;
SampledWork active() {return {12,1000,.25,12,true,true,false,false,false,false,false};}
void payload_progress_and_stall_boundary() {
    SampledWorkProgress progress;
    auto work=active();progress.observe(work,at(0ms));
    check(!progress.advancing(at(0ms))&&!progress.progress_age(at(0ms)),
        "One observed state does not prove transmission progress");
    work.fraction=.26;progress.observe(work,at(5s));
    check(progress.advancing(at(5s))&&progress.progress_age(at(5s))==0,
        "Increasing generated fraction did not establish sampled work");
    progress.observe(work,at(34s));
    check(progress.advancing(at(35s))&&progress.progress_age(at(35s))==30,
        "The thirty-second recent-work boundary changed");
    check(!progress.advancing(at(35001ms)),"Unchanged polling concealed a stalled transmission");
    work.media_seconds=13;progress.observe(work,at(36s));
    check(progress.advancing(at(36s)),"Increasing transmitted media time did not establish renewed work");
    check(!progress.advancing(at(35999ms)),"A backwards observation clock qualified as recent work");
}
void only_tail_samples_count() {
    SampledWorkProgress progress;
    auto work=active();work.fraction=0;work.media_seconds=0;
    progress.observe(work,at(0ms));
    work.samples+=100;progress.observe(work,at(1s));
    check(!progress.advancing(at(1s)),"Idle samples during preparation were mistaken for transmitted work");
    work.tail=true;work.fraction=1;work.media_seconds=48;
    progress.observe(work,at(2s));
    progress.observe(work,at(33s));
    check(!progress.advancing(at(33s)),"Entering the tail hid a later scoring stall");
    work.samples+=100;progress.observe(work,at(34s));
    check(progress.advancing(at(34s)),"Advancing physical-tail samples did not establish sampled work");
    progress.observe(work,at(65s));
    check(!progress.advancing(at(65s)),"Unchanged tail samples concealed a scoring stall");
}
void invalid_and_unrelated_work() {
    for(unsigned kind=0;kind<11;++kind) {
        SampledWorkProgress progress;auto work=active();progress.observe(work,at(0ms));
        work.fraction=.3;progress.observe(work,at(1s));
        check(progress.advancing(at(1s)),"Fixture failed to establish forward work");
        auto invalid=work;
        switch(kind) {
        case 0:invalid.simulation=false;break;
        case 1:invalid.transmitting=false;break;
        case 2:invalid.noise=true;break;
        case 3:invalid.replay=true;break;
        case 4:invalid.finished=true;break;
        case 5:invalid.failed=true;break;
        case 6:invalid.transmission=0;break;
        case 7:invalid.fraction=std::numeric_limits<double>::quiet_NaN();break;
        case 8:invalid.fraction=1.01;break;
        case 9:invalid.media_seconds=std::numeric_limits<double>::infinity();break;
        case 10:invalid.media_seconds=-1;break;
        }
        progress.observe(invalid,at(2s));
        check(!progress.advancing(at(2s)),"Inactive, invalid, replay, noise or failed state retained a budget exception");
        progress.observe(work,at(3s));
        check(!progress.advancing(at(3s)),"Returning from an ineligible state reused preceding progress");
    }
}
void independent_transmissions_and_regressions() {
    SampledWorkProgress progress;auto work=active();progress.observe(work,at(0ms));
    work.media_seconds+=1;progress.observe(work,at(1s));
    ++work.transmission;work.media_seconds=0;work.fraction=0;
    progress.observe(work,at(2s));
    check(!progress.advancing(at(2s)),"A new transmission inherited another transmission's progress");
    work.media_seconds=1;work.fraction=.1;progress.observe(work,at(3s));
    check(progress.advancing(at(3s)),"New transmission could not establish independent progress");
    work.fraction=.05;progress.observe(work,at(4s));
    check(!progress.advancing(at(4s)),"Regressing generated fraction qualified as forward work");
    work.fraction=.2;progress.observe(work,at(5s));
    work.media_seconds=2;progress.observe(work,at(6s));
    check(!progress.advancing(at(6s)),"Later increments concealed an earlier regression in the same transmission");
    ++work.transmission;progress.observe(work,at(7s));
    work.media_seconds=3;progress.observe(work,at(8s));
    check(progress.advancing(at(8s)),"A new transmission retained the old transmission's disqualification");
    work.media_seconds=0;progress.observe(work,at(9s));
    check(!progress.advancing(at(9s)),"Regressing media time retained preceding progress");
    ++work.transmission;progress.observe(work,at(10s));
    work.media_seconds=2;progress.observe(work,at(11s));
    check(progress.advancing(at(11s)),"Sample-regression fixture failed to establish progress");
    --work.samples;progress.observe(work,at(12s));
    check(!progress.advancing(at(12s)),"Regressing sample count retained preceding progress");
}
void observed_errors_remain_failures() {
    SampledWorkProgress progress;auto work=active();progress.observe(work,at(0ms));
    work.fraction=.3;progress.observe(work,at(1s));
    work.failed=true;progress.observe(work,at(2s));
    work.failed=false;work.fraction=.4;progress.observe(work,at(3s));
    work.fraction=.5;progress.observe(work,at(4s));
    check(!progress.advancing(at(4s)),"Later progress concealed a reported error in the same transmission");
    ++work.transmission;progress.observe(work,at(5s));
    work.fraction=.6;progress.observe(work,at(6s));
    check(progress.advancing(at(6s)),"A later independent transmission retained a preceding error");
}
void structured_result() {
    auto work=active();work.transmission=13;work.fraction=.032;work.media_seconds=4.275;work.samples=2928175;
    const SmokeBudgetExhausted exhausted(17,600.011,600,work,.001);
    check(smoke_budget_exit_code!=0&&smoke_budget_exit_code==75,"Budget exhaustion must remain a dedicated nonzero result");
    check(std::string(smoke_budget_marker)+exhausted.what()==
        "INCOMPLETE GUI_SMOKE_BUDGET: phase=17 elapsed=600.011000 budget=600.000000 tx_id=13 fraction=0.032000 media_seconds=4.275000 samples=2928175 tail=0 progress_age=0.001000 result=incomplete",
        "Budget exhaustion lost its stable measured incomplete-result diagnostic");
}
}
int main() {
    try {
        payload_progress_and_stall_boundary();only_tail_samples_count();invalid_and_unrelated_work();
        independent_transmissions_and_regressions();observed_errors_remain_failures();structured_result();
        std::cout<<"GUI smoke workload budget policy passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
