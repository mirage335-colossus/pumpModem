#include "datapump/helper_soft_joint_dwell.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace datapump::helper {
namespace {
using M4 = std::array<std::array<double,4>,4>;
using M2 = std::array<std::array<double,2>,2>;
constexpr double h[2][4]={{0,0,1,-1},{1,-1,0,0}};

template<std::size_t N> bool covariance_ok(const std::array<std::array<double,N>,N>& a) {
    // Pivoted LDL isn't necessary for these tiny matrices: test PSD through
    // symmetric Jacobi diagonalization, tolerating floating-point roundoff.
    auto b=a;
    double scale=0;
    for (std::size_t i=0;i<N;++i) for(std::size_t j=0;j<N;++j) {
        if (!std::isfinite(a[i][j])) return false;
        scale=std::max(scale,std::abs(a[i][j]));
        if (std::abs(a[i][j]-a[j][i])>1e-10*std::max(scale,1e-24)) return false;
    }
    for (unsigned it=0;it<60;++it) {
        std::size_t p=0,q=1; double max=0;
        for(std::size_t i=0;i<N;++i) for(std::size_t j=i+1;j<N;++j)
            if(std::abs(b[i][j])>max) { max=std::abs(b[i][j]); p=i; q=j; }
        if(max<=1e-14*std::max(scale,1e-24)) break;
        const double angle=.5*std::atan2(2*b[p][q],b[q][q]-b[p][p]);
        const double c=std::cos(angle),s=std::sin(angle);
        const double pp=b[p][p],qq=b[q][q],pq=b[p][q];
        b[p][p]=c*c*pp-2*s*c*pq+s*s*qq;
        b[q][q]=s*s*pp+2*s*c*pq+c*c*qq;
        b[p][q]=b[q][p]=0;
        for(std::size_t k=0;k<N;++k) if(k!=p && k!=q) {
            const double kp=b[k][p],kq=b[k][q];
            b[k][p]=b[p][k]=c*kp-s*kq;
            b[k][q]=b[q][k]=s*kp+c*kq;
        }
    }
    for(std::size_t i=0;i<N;++i) if(b[i][i]<-1e-10*std::max(scale,1e-24)) return false;
    return true;
}
template<std::size_t N> double quadratic(const std::array<double,N>& x,
        const std::array<std::array<double,N>,N>& c) {
    double v=0; for(std::size_t i=0;i<N;++i) for(std::size_t j=0;j<N;++j) v+=x[i]*c[i][j]*x[j];
    return std::sqrt(std::max(0.,v));
}
}

HelperSoftJointResult condition_helper_joint_dwell(const HelperJointDwellResult& fresh,
        const HelperSoftJointPrediction& prior,const HelperSoftJointConfig& cfg) {
    HelperSoftJointResult out;
    const auto& r=fresh.observation; const auto& m=r.measurement;
    if (!fresh.valid || !r.valid || !m.valid || !r.thermal_covariance_valid || !r.process_covariance_valid ||
        !r.frequency_dates_valid || !prior.valid || !prior.previous_observations_only || !prior.reference_preserved || prior.alternatives!=1 ||
        !std::isfinite(prior.time_seconds) || !std::isfinite(prior.latest_observation_seconds) ||
        !std::isfinite(prior.reference_input_seconds) || !std::isfinite(r.center_time_seconds) ||
        !std::isfinite(m.first_sample_time_seconds) || !std::isfinite(m.end_sample_time_seconds) ||
        !(m.first_sample_time_seconds <= r.center_time_seconds && r.center_time_seconds < m.end_sample_time_seconds) ||
        prior.reference_input_seconds > m.first_sample_time_seconds ||
        !std::isfinite(r.high_frequency_time_seconds) || !std::isfinite(r.low_frequency_time_seconds) ||
        std::abs(r.high_frequency_time_seconds-r.center_time_seconds)>1e-6 ||
        std::abs(r.low_frequency_time_seconds-r.center_time_seconds)>1e-6 ||
        std::abs(prior.time_seconds-r.center_time_seconds)>1e-6 ||
        prior.latest_observation_seconds>=m.first_sample_time_seconds ||
        r.center_time_seconds-prior.latest_observation_seconds>cfg.maximum_prediction_age_seconds ||
        !(cfg.maximum_prediction_age_seconds>0) || !std::isfinite(cfg.maximum_prediction_age_seconds) ||
        !(cfg.consistency_sigma_multiplier>0) || !std::isfinite(cfg.consistency_sigma_multiplier) ||
        !(cfg.maximum_phase_innovation_support_radians>0) ||
        !(cfg.maximum_phase_innovation_support_radians<std::numbers::pi) ||
        !covariance_ok(r.pilot_thermal_covariance) || !covariance_ok(r.pilot_process_covariance) ||
        !covariance_ok(prior.thermal_covariance) || !covariance_ok(prior.process_covariance)) return out;
    const std::array<double,4> x={m.high_frequency_hz,m.low_frequency_hz,r.high_center_phase_radians,r.low_center_phase_radians};
    const std::array<double,4> bound={r.high_frequency_model_half_width_hz,r.low_frequency_model_half_width_hz,
        fresh.common_phase_model_half_width_radians,fresh.common_phase_model_half_width_radians+fresh.phase_difference_model_half_width_radians};
    const std::array<double,2> pb={prior.phase_model_half_width_radians,prior.frequency_model_half_width_hz};
    for(double v:x) if(!std::isfinite(v)) return out;
    for(double v:bound) if(!std::isfinite(v) || v<0) return out;
    for(double v:pb) if(!std::isfinite(v) || v<0) return out;
    if (!std::isfinite(prior.phase_difference_radians) || !std::isfinite(prior.frequency_difference_hz)) return out;
    const double innov[2]={std::remainder(prior.phase_difference_radians-(x[2]-x[3]),2*std::numbers::pi),
                           prior.frequency_difference_hz-(x[0]-x[1])};
    M4 c{}; M2 s{}; double ch[4][2]{};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) c[i][j]=r.pilot_thermal_covariance[i][j]+r.pilot_process_covariance[i][j];
    for(int i=0;i<4;++i) for(int a=0;a<2;++a) for(int j=0;j<4;++j) ch[i][a]+=c[i][j]*h[a][j];
    for(int a=0;a<2;++a) for(int b=0;b<2;++b) {
        s[a][b]=prior.thermal_covariance[a][b]+prior.process_covariance[a][b];
        for(int i=0;i<4;++i) s[a][b]+=h[a][i]*ch[i][b];
    }
    // Innovation supports use L1 across current and previous, even if common
    // oscillator increments or model selection correlate them.
    double supports[2]{};
    for(int a=0;a<2;++a) {
        std::array<double,4> row{}; for(int i=0;i<4;++i) row[i]=h[a][i];
        std::array<double,2> unit{}; unit[a]=1;
        const double thermal=quadratic(row,r.pilot_thermal_covariance)+quadratic(unit,prior.thermal_covariance);
        const double process=quadratic(row,r.pilot_process_covariance)+quadratic(unit,prior.process_covariance);
        double b=pb[a]; for(int i=0;i<4;++i) b+=std::abs(row[i])*bound[i];
        supports[a]=cfg.consistency_sigma_multiplier*(thermal+process)+b;
    }
    out.phase_innovation_radians=innov[0]; out.frequency_innovation_hz=innov[1];
    out.phase_innovation_support_radians=supports[0]; out.frequency_innovation_support_hz=supports[1];
    if (!std::isfinite(supports[0]) || !std::isfinite(supports[1]) ||
        supports[0]>=cfg.maximum_phase_innovation_support_radians ||
        std::abs(innov[0])>supports[0] || std::abs(innov[1])>supports[1]) return out;
    const double det=s[0][0]*s[1][1]-s[0][1]*s[1][0];
    if (!(det>1e-20) || !std::isfinite(det)) return out;
    const double inv[2][2]={{s[1][1]/det,-s[0][1]/det},{-s[1][0]/det,s[0][0]/det}};
    for(int i=0;i<4;++i) {
        std::array<double,2> k{};
        for(int a=0;a<2;++a) for(int b=0;b<2;++b) k[a]+=ch[i][b]*inv[b][a];
        std::array<double,4> a{}; a[i]=1;
        for(int j=0;j<4;++j) for(int t=0;t<2;++t) a[j]-=k[t]*h[t][j];
        out.value[i]=x[i]+k[0]*innov[0]+k[1]*innov[1];
        out.thermal_sigma[i]=quadratic(a,r.pilot_thermal_covariance)+quadratic(k,prior.thermal_covariance);
        out.process_sigma[i]=quadratic(a,r.pilot_process_covariance)+quadratic(k,prior.process_covariance);
        for(int j=0;j<4;++j) out.model_half_width[i]+=std::abs(a[j])*bound[j];
        for(int j=0;j<2;++j) out.model_half_width[i]+=std::abs(k[j])*pb[j];
        if(!std::isfinite(out.value[i]) || !std::isfinite(out.thermal_sigma[i]) ||
           !std::isfinite(out.process_sigma[i]) || !std::isfinite(out.model_half_width[i])) return HelperSoftJointResult{};
    }
    out.time_seconds=r.center_time_seconds; out.reference_input_seconds=prior.reference_input_seconds;
    out.first_sample_time_seconds=m.first_sample_time_seconds; out.end_sample_time_seconds=m.end_sample_time_seconds;
    out.valid=true; out.fallback_required=false; return out;
}
} // namespace datapump::helper
