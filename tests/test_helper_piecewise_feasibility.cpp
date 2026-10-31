// Exploratory falsification, not a receiver: frequency coordinates use
// simulator truth plus a controlled offset, and neither selected aperture nor
// fitted phase is fed back into tracking. The output reports common admitted
// dwells and does not claim post-selection uncertainty or full-chain behavior.
#include "helper_live_probe_fixture.hpp"
#include "datapump/helper_aided_phase.hpp"
#include <complex>
#include <numbers>
#include <iostream>
#include <cmath>
#include <array>
#include <algorithm>
#include <span>
#include <string>
namespace p=helper_live_probe;namespace h=datapump::helper;
constexpr double fs=p::sample_rate,pi=std::numbers::pi,tau=2*pi;using C=std::complex<double>;double wrap(double x){return std::remainder(x,tau);}
struct Fit {double cost=1e99,delta=0;int split=0;C left{},right{};};
int main(int argc,char**argv){int count=argc>1?std::stoi(argv[1]):4,first=argc>2?std::stoi(argv[2]):16777250;double onset=argc>3?std::stod(argv[3]):0.,duration=argc>4?std::stod(argv[4]):0.,amplitude=argc>5?std::stod(argv[5]):1.,phaseoff=argc>6?std::stod(argv[6]):pi/2.,offset=argc>7?std::stod(argv[7]):.15;std::string mode=argc>8?argv[8]:"high";double threshold=argc>9?std::stod(argv[9]):8.;int attempt=0,basev=0,flagged=0,admitted=0,withheld=0,sidecorrect=0;double baseall=0,baseline=0,cand=0,maxb=0,maxc=0,fb=0,fc=0;int maxout=0;double evidence_sum=0,evidence_max=0;
for(int seed=first;seed<first+count;seed++){p::Config cfg;cfg.seed=seed;p::Source src(cfg);for(unsigned d=0;d<6;d++){auto dw=src.generate(d);if(d<2)continue;auto in=std::span<std::complex<float>>(dw.samples).subspan(480,8640);double t=dw.first_seconds+(480+4319.5)/fs;double hf=2400*src.audio_scale_at(t)+src.shift_error_hz(dw.shift_hz,t),lf=550*src.audio_scale_at(t)+src.shift_error_hz(dw.shift_hz,t);C ph=(C(dw.common_phasor[479+4320])+C(dw.common_phasor[480+4320]))*.5;double hp=wrap(std::arg(ph)+tau*std::remainder(2400*src.tx_time(t),1.));double lp=wrap(std::arg(ph)+tau*std::remainder(550*src.tx_time(t),1.));double pd=((seed+d)&1)?offset:-offset;
for(size_t i=0;i<in.size();i++){double ts=double(i)/fs;if(ts>=onset&&ts<onset+duration){double rel=(double(i)-4319.5)/fs;
if(mode=="high"||mode=="both")in[i]+=std::complex<float>(std::polar(amplitude,tau*hf*rel+hp+phaseoff));if(mode=="both")in[i]+=std::complex<float>(std::polar(amplitude,tau*lf*rel+lp+phaseoff));if(mode=="chirp")in[i]+=std::complex<float>(std::polar(amplitude,tau*(hf*rel+2.*rel*rel)+hp+phaseoff));}}
h::HelperAidedPhaseConfig c;c.sample_rate=fs;h::HelperAidedPhasePrediction pr{true,hf+pd,lf+pd,.2,.2};auto base=h::estimate_helper_aided_phase(in,c,pr,dw.shift_hz,dw.first_seconds+480/fs);attempt++;if(!base.valid)continue;basev++;double original=wrap(base.high_center_phase_radians-hp);baseall+=original*original;
std::array<C,4> z{},zl{};double weight=0,weightsq=0;for(int j=0;j<4;j++)for(int i=0;i<2160;i++){int k=j*2160+i;double w=.5-.5*std::cos(tau*i/2159.),rel=(double(k)-4319.5)/fs;z[j]+=C(in[k])*w*std::polar(1.,-tau*pr.high_frequency_hz*rel);zl[j]+=C(in[k])*w*std::polar(1.,-tau*pr.low_frequency_hz*rel);if(j==0){weight+=w;weightsq+=w*w;}}
for(C&v:z)v/=weight;for(C&v:zl)v/=weight;double effective=weight*weight/(fs*weightsq),varianceh=2*base.high_raw_noise_density/effective,variancel=2*base.low_raw_noise_density/effective;
Fit smooth,change;for(int g=-15;g<=15;g++){double delta=.05*g;std::array<C,4>x{},y{};for(int k=0;k<4;k++){C rot=std::polar(1.,-tau*delta*(k-1.5)*.09);x[k]=z[k]*rot;y[k]=zl[k]*rot;}C avg=(x[0]+x[1]+x[2]+x[3])*.25,avg2=(y[0]+y[1]+y[2]+y[3])*.25;double ss=0;for(int k=0;k<4;k++)ss+=std::norm(x[k]-avg)/varianceh+std::norm(y[k]-avg2)/variancel;if(ss<smooth.cost)smooth={ss,delta,0,avg,avg};for(int split=1;split<=3;split++){C a{},b{},c{},d{};for(int k=0;k<4;k++){(k<split?a:b)+=x[k];(k<split?c:d)+=y[k];}a/=double(split);b/=double(4-split);c/=double(split);d/=double(4-split);double score=0;for(int k=0;k<4;k++)score+=std::norm(x[k]-(k<split?a:b))/varianceh+std::norm(y[k]-(k<split?c:d))/variancel;if(score<change.cost)change={score,delta,split,a,b};}}
double evidence=smooth.cost-change.cost;evidence_sum+=evidence;evidence_max=std::max(evidence_max,evidence);bool changed=evidence>threshold;if(changed)flagged++;if(changed&&change.split==2){withheld++;continue;}int side=changed?(change.split==1?1:0):-1;double outphase=base.high_center_phase_radians,date=t;double f=base.measurement.high_frequency_hz;
if(changed){int from=side?change.split:0,to=side?4:change.split;date=dw.first_seconds+(480+((from+to)*.5*2160)-.5)/fs;C a=side?change.right:change.left;outphase=wrap(std::arg(a)+tau*(pr.high_frequency_hz+change.delta)*(date-t));f=pr.high_frequency_hz+change.delta;
// Evaluation only: whether the selected interval intersects the injected transient.
double l=from*.09,r=to*.09;if(duration>0&&!(l<onset+duration&&r>onset))sidecorrect++;
}
double truthdate=date,q=(truthdate-dw.first_seconds)*fs;size_t qi=size_t(std::floor(q));double frac=q-double(qi);double cp=std::arg(C(dw.common_phasor[qi]))+frac*wrap(std::arg(C(dw.common_phasor[qi+1]))-std::arg(C(dw.common_phasor[qi])));double truth=wrap(cp+tau*std::remainder(2400*src.tx_time(truthdate),1.));double error=wrap(outphase-truth);admitted++;baseline+=original*original;cand+=error*error;maxb=std::max(maxb,std::abs(original));maxc=std::max(maxc,std::abs(error));fb+=std::pow(base.measurement.high_frequency_hz-hf,2);fc+=std::pow(f-hf,2);if(std::abs(error)>.2)maxout++;
}}
std::cout<<"attempt="<<attempt<<" baseline_valid="<<basev<<" changed="<<flagged<<" withheld="<<withheld<<" admitted="<<admitted<<" selected_clean="<<sidecorrect<<" baseline_all_deg="<<sqrt(baseall/basev)*180/pi<<" baseline_common_deg="<<sqrt(baseline/admitted)*180/pi<<" candidate_deg="<<sqrt(cand/admitted)*180/pi<<" base_max_deg="<<maxb*180/pi<<" candidate_max_deg="<<maxc*180/pi<<" evidence_mean="<<evidence_sum/basev<<" evidence_max="<<evidence_max<<" candidate_bad="<<maxout<<" baseline_freq_rms="<<sqrt(fb/admitted)<<" candidate_freq_rms="<<sqrt(fc/admitted)<<"\n";}
