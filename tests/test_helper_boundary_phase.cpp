#include "datapump/helper_boundary_phase.hpp"
#include "helper_live_probe_fixture.hpp"
#include "helper_tracking_fixture.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace h=datapump::helper;namespace p=helper_live_probe;namespace t=helper_tracking;
void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
int main(){try{
 for(double loss:{180.,170.,160.}){unsigned valid=0,early=0,late=0,retune_early=0,retune_valid=0;double er=0,lr=0;
 for(unsigned seed=1700000001U;seed<1700000009U;++seed){p::Config cfg;cfg.seed=seed;cfg.expanded_schedule_key=true;cfg.path_loss_db=loss;
  t::Impairments imp;imp.late_retune_dwell=2;imp.late_retune_seconds=.025;t::Source s(cfg,imp); p::Source clean(cfg);
  for(unsigned k=0;k<3;++k){auto d=s.generate(k);auto normal=clean.generate(k);if(k!=2)continue;
   double center=d.first_seconds+.02+(8640.-1)/(2*p::sample_rate);
   h::HelperAidedPhaseConfig ac;ac.sample_rate=p::sample_rate;ac.compute_slope_covariance=true;
   ac.phase_noise_radians_per_sqrt_second=.05*std::numbers::pi/180;
   // These known fixture coordinates are diagnostic only; actual receiver
   // integration must supply causal previous-Helper predictions.
   h::HelperAidedPhasePrediction pred{true,2400*clean.audio_scale_at(center)+clean.shift_error_hz(d.shift_hz,center)+.1,
    550*clean.audio_scale_at(center)+clean.shift_error_hz(d.shift_hz,center)+.1,.2,.2};
   const auto eval=[&](const p::Dwell& x){auto a=h::estimate_helper_aided_phase(std::span<const std::complex<float>>(x.samples).subspan(480,8640),ac,pred,x.shift_hz,x.first_seconds+.02);
     h::HelperBoundaryPhaseConfig bc;bc.sample_rate=p::sample_rate;return h::estimate_helper_boundary_support(x.samples,x.first_seconds,a,bc);};
   const auto b=eval(normal),r=eval(d);
   if(b.evaluated){++valid;early+=b.early_admitted;late+=b.late_admitted;er+=b.early_conditional_radius_radians;lr+=b.late_conditional_radius_radians;}
   if(r.evaluated){++retune_valid;retune_early+=r.early_admitted;}
  }}
 std::cout<<"boundary_short loss_db="<<loss<<" valid="<<valid<<" early="<<early<<" late="<<late
  <<" early_radius_mean_rad="<<(valid?er/valid:0)<<" late_radius_mean_rad="<<(valid?lr/valid:0)
  <<" retune_valid="<<retune_valid<<" retune_early_admitted="<<retune_early<<'\n';
 require(valid>=4,"insufficient clean baseline");
 }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
