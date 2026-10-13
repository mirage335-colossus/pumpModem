#include "../src/scheduled_frequency_fusion.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
CorrelationFit rotated(const CorrelationFit& f,std::complex<double> phase) {
    CorrelationFit out;out.add({f.xc,f.xs,f.cc,f.ss,f.cs,f.energy},phase,static_cast<std::size_t>(f.count));return out;
}
CorrelationFit exact(std::span<const ScheduledFusionSample> samples,double a,ScheduledFusionPhase center={}) {
    CorrelationFit out;
    for(const auto& s:samples) {
        const auto fit=rotated(s.coarse,center.value*std::polar(1.,a*s.normalized_time));
        out.xc+=fit.xc;out.xs+=fit.xs;out.cc+=fit.cc;out.ss+=fit.ss;out.cs+=fit.cs;
        out.energy+=fit.energy;out.count+=fit.count;
    }
    return out;
}
std::vector<ScheduledFusionSample> fixture(std::size_t n,double signal,double interference=0,bool clipping=false) {
    std::mt19937_64 random(9125763);std::normal_distribution<double> noise;
    std::vector<ScheduledFusionSample> values;values.reserve(n);
    for(std::size_t i=0;i<n;++i) {
        const auto u=n>1?2.*static_cast<double>(i)/static_cast<double>(n-1)-1:0;
        const auto q=std::polar(.2+.09*std::sin(17*u)+.03*std::cos(7*u),81*u+std::sin(3*u));
        auto x=static_cast<float>(signal*q.real()+.17*noise(random)+interference*std::cos(93*u));
        if(clipping)x=std::clamp(x,-.4F,.4F);
        const auto qr=q.real(),qi=q.imag();
        values.push_back({{x*qr,x*qi,qr*qr,qi*qi,qr*qi,double(x)*x,1},u});
    }
    return values;
}
void contains(const ScheduledFusionEvaluation& result,const CorrelationFit& original,const char* message) {
    check(result.supported,message);
    const auto de=std::hypot(result.fit.xc-original.xc,result.fit.xs-original.xs);
    const auto ce=std::max(std::abs(result.fit.cc-original.cc)+std::abs(result.fit.cs-original.cs),
        std::abs(result.fit.ss-original.ss)+std::abs(result.fit.cs-original.cs));
    check(de<=result.dot_error+1e-14,message);
    check(ce<=result.gram_error+1e-14,message);
    check(std::abs(result.fit.energy-original.energy)<=result.energy_error+1e-14,message);
    if(result.well_conditioned)check(original.score()>=result.score_lower && original.score()<=result.score_upper,message);
}
void arithmetic_and_boundaries() {
    std::size_t checked=0;
    for(auto arithmetic:{ScheduledFusionArithmetic::fp32,ScheduledFusionArithmetic::fp64})
        for(auto backend:{ScheduledFusionBackend::portable,ScheduledFusionBackend::native})
            for(const auto n:{3U,8U,17U,127U,128U,129U,4096U}) {
                const auto samples=fixture(n,.7,.2,n==129);
                ScheduledFrequencyFusion one(arithmetic,backend),fragmented(arithmetic,backend);
                check(one.add(samples),"eligible fusion sample rejected");
                for(std::size_t offset=0;offset<samples.size();) {
                    const auto count=std::min<std::size_t>(17,samples.size()-offset);
                    check(fragmented.add(std::span(samples).subspan(offset,count)),"fragmented accumulation rejected");offset+=count;
                }
                for(double a:{-.125,-.009817477042468,.0,.009817477042468,.125}) {
                    const auto phase=scheduled_fusion_phase(.0078125,8312.L*48000+317.375L,48000);
                    const auto original=exact(samples,a,phase);
                    contains(one.evaluate(a,phase),original,"native/portable certificate omitted original fine fit");
                    contains(fragmented.evaluate(a,phase),original,"block boundary changed error certificate");
                    ++checked;
                }
                check(one.count()==n && one.working_bytes()<1024 && one.scratch_bytes()<10000,
                    "fusion state or scratch grew with frequency or observation duration");
            }
    std::cout<<"conditional certificate comparisons="<<checked<<'\n';
}
void amplitude_interference_and_thresholds() {
    double maximum_error=0,maximum_width=0;
    for(double signal:{0.,.2,1.,8.})for(double interference:{0.,.7})for(bool clipped:{false,true}) {
        const auto samples=fixture(1024,signal,interference,clipped);
        const auto original=exact(samples,.0098);
        ScheduledFrequencyFusion fusion;check(fusion.add(samples),"amplitude/interference fixture rejected");
        const auto result=fusion.evaluate(.0098);contains(result,original,"amplitude/clipping certificate failed");
        check(result.well_conditioned,"ordinary noisy template unexpectedly ill-conditioned");
        maximum_error=std::max(maximum_error,std::abs(result.fit.score()-original.score()));
        maximum_width=std::max(maximum_width,result.score_upper-result.score_lower);
        for(double threshold:{original.score()-1,original.score(),original.score()+1}) {
            if(result.above(threshold))check(original.score()>threshold,"certificate falsely admitted threshold");
            if(result.below(threshold))check(original.score()<threshold,"certificate falsely rejected threshold");
        }
    }
    // This is an arithmetic/statistic test; no small noise fixture is a rare-
    // false-accept rate or a detection/BER curve.
    std::cout<<"conditional max_score_error="<<maximum_error<<" max_interval_width="<<maximum_width<<'\n';
}
void original_private_candidates() {
    for(unsigned factor:{1U,10U,100U,1000U}) {
        Config c;c.pattern_symbols=true;c.constellation_bits=1;c.sample_rate=8192;c.bandwidth_hz=64./factor;c.carrier_hz=1500;
        c.spreading_factor=128;c.scramble=true;c.dsss=factor>1;c.dsss_factor=factor;
        c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x73));
        for(std::size_t j=0;j<c.spreading_seed.size();++j) {
            c.spreading_seed[j]=static_cast<std::uint8_t>(3*j+7);
            c.dsss_seed[j]=static_cast<std::uint8_t>(5*j+11);
        }
        PatternCode code(c,8312);std::array<std::vector<ScheduledFusionSample>,2> samples;
        const auto first_chip=code.chips_per_symbol();code.prepare_symbol(first_chip);
        bool independent=false;
        for(std::size_t i=0;i<256;++i) {
            // Exact same original source/template coordinates at both arithmetic
            // settings, including fractional timing and a clock-boundary warp.
            const auto within=17.375+static_cast<double>(i)*1.0001;
            auto phases=code.shaped_values(first_chip,within);
            for(auto& p:phases)p=pattern_limit_pcm(.7*p)/.7;
            independent=independent || std::abs(phases[0]-phases[1])>1e-12;
            const auto carrier=std::polar(1.,2*std::numbers::pi*1500*i/c.sample_rate);
            const auto x=static_cast<float>((phases[0]*carrier).real()+.01*std::sin(.3*i));
            const CorrelationProjection projection{x*carrier.real(),x*carrier.imag(),
                carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),double(x)*x};
            for(unsigned bit=0;bit<2;++bit)samples[bit].push_back(scheduled_fusion_sample(projection,phases[bit],2.*i/255-1));
        }
        check(independent,"private0/1 fixture accidentally reused a candidate");
        for(const auto& row:samples)for(auto arithmetic:{ScheduledFusionArithmetic::fp32,ScheduledFusionArithmetic::fp64}) {
            ScheduledFrequencyFusion fusion(arithmetic);check(fusion.add(row),"private candidate moment construction rejected");
            contains(fusion.evaluate(.0098),exact(row,.0098),"private/DSSS candidate changed outside error certificate");
        }
    }
}
void explicit_fallbacks_and_coordinates() {
    auto values=fixture(128,1);
    ScheduledFrequencyFusion fusion;check(fusion.add(values),"baseline fixture rejected");
    check(!fusion.evaluate(.126).supported,"unqualified Taylor radius silently admitted");
    check(!ScheduledFrequencyFusion::reuse_worth_trying(8,1) &&
        ScheduledFrequencyFusion::reuse_worth_trying(4096,101),"small-piece measured crossover discarded");
    values[13].coarse.xc=std::numeric_limits<double>::infinity();
    ScheduledFrequencyFusion bad;check(!bad.add(values) && !bad.evaluate(0).supported,"nonfinite operands silently admitted");
    values=fixture(128,1);for(auto& v:values){v.coarse.xs=0;v.coarse.ss=0;v.coarse.cs=0;}
    ScheduledFrequencyFusion singular;check(singular.add(values),"singular input should produce an explicit scoring fallback");
    const auto s=singular.evaluate(0);check(s.supported && !s.well_conditioned && s.ambiguous(10),"singular Gram bypassed exact fallback");
    values=fixture(128,1);for(auto& v:values)v.coarse.xc=1e-100;
    ScheduledFrequencyFusion narrow;check(!narrow.add(values),"unsafe FP32 range silently flushed");
    ScheduledFrequencyFusion wide(ScheduledFusionArithmetic::fp64);check(wide.add(values),"finite original FP64 range rejected");
    values=fixture(4096,1);ScheduledFrequencyFusion bounded;check(bounded.add(values),"bounded piece rejected");
    check(!bounded.add(std::span(values).first(1)),"helper allowed unbounded later PCM refinement");
    const auto phase=scheduled_fusion_phase(.0078125,1e12L+.375L,48000);
    check(phase.supported,"ordinary large physical origin lost wide phase precision");
    const auto moved=scheduled_fusion_phase(.0078125,1e12L+48000+.375L,48000);
    const auto expected=phase.value*std::polar(1.,2*std::numbers::pi*.0078125);
    check(std::abs(moved.value-expected)<=phase.error+moved.error+1e-14,"physical center phase truncated to float or integer seconds");
    check(!scheduled_fusion_phase(1000,1e30L,48000).supported,"unbounded absolute phase error silently accepted");
    // Preserve original rank-one rotation residues; never clip the Gram
    // solely to make a native arithmetic path appear eligible.
    auto residue=fixture(128,1);
    for(auto& item:residue) {item.coarse.cc=-2*std::numeric_limits<double>::epsilon();item.coarse.ss=1;item.coarse.cs=0;}
    ScheduledFrequencyFusion finite_residue(ScheduledFusionArithmetic::fp64);
    check(finite_residue.add(residue),"finite quadrature-zero Gram rounding residue rejected");
    const auto finite_fit=finite_residue.evaluate(0);
    check(finite_fit.supported && !finite_fit.well_conditioned && finite_fit.fit.cc<0,
        "rank-one rounding residue was clipped or bypassed conditioning fallback");
    for(auto& item:residue) {item.coarse.cc=1;item.coarse.ss=1;item.coarse.cs=1.001;}
    ScheduledFrequencyFusion indefinite;
    check(!indefinite.add(residue),"materially indefinite Gram passed rounding envelope");
    // Certificates bound every accepted diagnostic dot sequence, without
    // assuming a Cauchy relationship not enforced by this numerical API.
    auto independent_dot=fixture(128,1);
    for(auto& item:independent_dot){item.coarse.xc*=1e6;item.coarse.xs*=1e6;}
    ScheduledFrequencyFusion arbitrary_dot;
    check(arbitrary_dot.add(independent_dot),"finite diagnostic dots rejected by undocumented energy precondition");
    contains(arbitrary_dot.evaluate(.125),exact(independent_dot,.125),"Taylor dot certificate relied on unvalidated Cauchy bound");
    // A global fit can carry summed piece certificates without promising that
    // discarded old PCM can be refined later.
    const auto total=ScheduledFrequencyFusion::certify(exact(fixture(256,.3),0),1e-10,1e-10,1e-12);
    check(total.supported,"summed piece-certificate interface rejected finite evidence");
}
}
int main(){try {
    arithmetic_and_boundaries();amplitude_interference_and_thresholds();original_private_candidates();
    explicit_fallbacks_and_coordinates();std::cout<<"scheduled frequency fusion conditional arithmetic checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
