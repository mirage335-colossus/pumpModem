#include "datapump/helper_soft_joint_dwell.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace datapump::helper;
int main() {
    HelperJointDwellResult f; f.valid=true;
    auto& r=f.observation; r.valid=true; r.thermal_covariance_valid=true; r.process_covariance_valid=true;
    r.center_time_seconds=10.2; r.frequency_dates_valid=true;
    r.high_frequency_time_seconds=r.low_frequency_time_seconds=10.2; r.measurement.valid=true; r.measurement.first_sample_time_seconds=10.04;
    r.measurement.end_sample_time_seconds=10.36; r.measurement.high_frequency_hz=2400.10;
    r.measurement.low_frequency_hz=550.0; r.high_center_phase_radians=.4; r.low_center_phase_radians=.2;
    for (int i=0;i<4;++i) { r.pilot_thermal_covariance[i][i]=.01; r.pilot_process_covariance[i][i]=.0001; }
    // Correlated phase-frequency local evidence must influence both outputs.
    r.pilot_thermal_covariance[0][2]=r.pilot_thermal_covariance[2][0]=.005;
    HelperSoftJointPrediction p; p.valid=p.previous_observations_only=p.reference_preserved=true;
    p.alternatives=1; p.time_seconds=10.2; p.latest_observation_seconds=9.9; p.reference_input_seconds=1.0;
    p.phase_difference_radians=.1; p.frequency_difference_hz=1850.0;
    p.thermal_covariance[0][0]=p.thermal_covariance[1][1]=.001;
    p.process_covariance[0][0]=p.process_covariance[1][1]=.0001;
    auto o=condition_helper_joint_dwell(f,p);
    assert(o.valid && !o.fallback_required && o.reference_input_seconds==1.0);
    assert(o.value[0]<2400.10 && o.value[2]<.4);
    assert(o.thermal_sigma[0]>0 && o.process_sigma[0]>0);
    assert(o.first_sample_time_seconds==10.04 && o.end_sample_time_seconds==10.36);
    // Prior is not allowed to include the current dwell or select a new reference.
    auto bad=p; bad.latest_observation_seconds=10.1;
    assert(!condition_helper_joint_dwell(f,bad).valid);
    bad=p; bad.reference_preserved=false;
    assert(!condition_helper_joint_dwell(f,bad).valid);
    bad=p; bad.alternatives=2;
    assert(!condition_helper_joint_dwell(f,bad).valid);
    // Contradictory relationship and unsound covariance must fall back.
    bad=p; bad.frequency_difference_hz+=10;
    assert(!condition_helper_joint_dwell(f,bad).valid);
    bad=p; bad.thermal_covariance[0][1]=bad.thermal_covariance[1][0]=1;
    assert(!condition_helper_joint_dwell(f,bad).valid);
    // Small absolute negative variance must not disappear into a tolerance.
    bad=p; bad.process_covariance[0][0]=-1e-11;
    assert(!condition_helper_joint_dwell(f,bad).valid);
    auto bad_f=f; bad_f.observation.high_frequency_time_seconds=10.1;
    assert(!condition_helper_joint_dwell(bad_f,p).valid);
    HelperSoftJointConfig unbounded; unbounded.maximum_phase_innovation_support_radians=4;
    assert(!condition_helper_joint_dwell(f,p,unbounded).valid);
    // Random fresh hop phase shifts both pilots and the result equally.
    auto shifted=f; shifted.observation.high_center_phase_radians+=1.2;
    shifted.observation.low_center_phase_radians+=1.2;
    auto so=condition_helper_joint_dwell(shifted,p);
    assert(so.valid && std::abs((so.value[2]-o.value[2])-1.2)<1e-10);
    // The unknown fixed differential phase is supplied by the previous fit.
    // Changing the low-pilot gauge and reference consistently preserves H.
    shifted=f; shifted.observation.low_center_phase_radians+=.5;
    bad=p; bad.phase_difference_radians-=.5;
    so=condition_helper_joint_dwell(shifted,bad);
    assert(so.valid && std::abs(so.value[2]-o.value[2])<1e-10);
    std::cout << "helper_soft_joint_dwell short checks passed\n";
}
