#pragma once
#include "datapump/helper_phase_precision.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <string>
namespace datapump::helper {
struct TonePrecisionRequest {
    std::uint32_t sample_rate=0;
    double straight_cn0_db_hz=0,phase_noise_radians_per_sqrt_second=0;
    std::size_t metadata_symbols=3;
    bool punctuate=true;
    double baseline_lead_seconds=2,target_half_width_hz=.1,max_extra_seconds=36;
    double maximum_tracking_slew_hz_per_second=.5,maximum_source_capture_ratio=1;
    // Pure pilot: phase is an independent nuisance after every radio retune.
    // The legacy modulated-tone forecast remains available for its controls.
    bool pilot_segmented=false;
    double pilot_coherent_aperture_seconds=.2,pilot_estimator_efficiency=1.25;
};
struct TonePrecisionPrediction {
    // supported grants a numeric precision estimate, not measured lock. A valid
    // request can select capped airtime while precision remains unsupported.
    bool supported=false,target_reached=false,extension_capped=false;
    // A separate conditional clean-model estimate at the already bounded
    // baseline+maximum-extra aperture. This is NOT selected transmit lead,
    // measured noisy-waveform precision, or a runtime qualification date.
    bool sustained_supported=false;
    double baseline_lead_seconds=0,extra_lead_seconds=0,total_lead_seconds=0;
    // Numeric precision/support fields are meaningful only when supported;
    // their zero defaults never mean zero measured uncertainty.
    // Initial clean-model precision at total_lead_seconds. Measured purity,
    // excess and cycle losses can make actual noisy-waveform precision wider.
    double frequency_half_width_hz=0,usable_tone_seconds=0;
    // Zero U is unavailable unless sustained_supported; at_seconds merely
    // identifies the bounded forecast aperture and adds no transmit delay.
    double sustained_at_seconds=0,sustained_frequency_half_width_hz=0;
    double coherent_aperture_seconds=0,segmented_noise_reference_half_width_hz=0;
    // Explicit conditional planning allowances; defaults preserve the legacy
    // straight-tone forecast. Neither is an observed loss or a physical floor.
    double fallback_information_fraction=1,fallback_frequency_grid_hz=0;
    std::size_t expected_segments=0;
    std::string reason;
};
namespace precision_detail {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t segment_edges=16,segment_records=256;
constexpr double recent_seconds=36;
struct Edge {double hz=0,first_variance=0,last_variance=0;};
struct Segment {std::uint64_t first=0,end=0;double hz=0,variance=0;};
struct Estimate {bool valid=false;double hz=0,slope=0,uncertainty=0,slope_uncertainty=0,chi=0;std::uint64_t first=0;std::size_t count=0;};
// Linear phase regression expressed as parabolic weights on consecutive
// frequency differences. Only equal ACTUAL saved shared endpoints cancel.
inline bool make_segment(std::span<const Edge> edges,double dt,double sigma,
        std::uint64_t first,std::uint64_t end,Segment& result)noexcept {
    const auto m=edges.size(),n=m+1;if(m<4 || m>segment_edges || !(dt>0) || !(sigma>=0) || end<=first)return false;
    const auto base=edges.front().hz;double correction=0,thermal=0,previous_weight=0,norm=0;
    for(std::size_t k=0;k<m;++k) {
        const auto& e=edges[k];
        if(!std::isfinite(e.hz) || !std::isfinite(e.first_variance) || !std::isfinite(e.last_variance) || e.first_variance<0 || e.last_variance<0 ||
           (k && e.first_variance!=edges[k-1].last_variance))return false;
        const auto w=6.*(k+1)*(m-k)/(static_cast<double>(m)*(m+1)*(m+2));
        correction+=w*(e.hz-base);norm+=w*w;
        thermal+=e.first_variance*(previous_weight-w)*(previous_weight-w);previous_weight=w;
    }
    thermal+=edges.back().last_variance*previous_weight*previous_weight;
    const auto tt=dt*dt*n*m*(m+2)/12.;
    double phase=0,mean=0;
    for(std::size_t j=0;j<n;++j) {mean+=phase;if(j<m)phase+=dt*(edges[j].hz-base);}
    mean/=n;phase=0;double residual=0,expected=0;
    for(std::size_t j=0;j<n;++j) {
        const auto u=(static_cast<double>(j)-.5*m)*dt;
        const auto error=phase-mean-correction*u;residual+=error*error;
        const auto v=j?edges[j-1].last_variance:edges[0].first_variance;
        expected+=v*dt*dt*std::max(0.,1-1./n-u*u/tt);
        if(j<m)phase+=dt*(edges[j].hz-base);
    }
    // Deliberately do not subtract configured Wiener residual here. Keeping
    // it in measured excess is conservative when a real segment is non-CW.
    const auto excess=std::max(0.,(residual-expected)/(n-2));
    const auto process=sigma*sigma/(tau*tau*dt)*norm;
    const auto variance=thermal+process+excess/tt;
    if(!(variance>0) || !std::isfinite(variance))return false;
    result={first,end,base+correction,variance};return std::isfinite(result.hz);
}
// The full and newest-half/quarter current fits share summaries. Their exact
// difference covariance and actual quadratic response determine a dated
// smooth-evolution bias allowance; an operational slew setting is not a
// physical acceleration prior. A jump or faster unresolved evolution is not
// guaranteed by this model and must retain the independent current FLL veto.
inline Estimate pool(std::span<const Segment> segments,const Segment* current,
        std::uint64_t now,std::uint32_t rate,double sigma)noexcept {
    Estimate result;if(!rate || now==UINT64_MAX)return result;
    const auto count=segments.size()+(current?1:0);if(count<8)return result;
    const auto get=[&](std::size_t i)->const Segment&{return i<segments.size()?segments[i]:*current;};
    const auto first=get(0).first;if(now<first)return result;
    const auto support=static_cast<double>(now-first+1)/rate;if(support<1)return result;
    const auto base=get(0).hz;
    const auto time=[&](const Segment& s) {return ((static_cast<long double>(s.first)-now)+.5L*(s.end-s.first-1))/rate/support;};
    struct Line {
        long double m00=0,m01=0,m11=0,y0=0,y1=0,determinant=0;
        double hz=0,slope=0,scale=1;std::size_t count=0;bool valid=false;
        long double weight(long double t,double variance)const noexcept {return (m11-m01*t)/determinant/variance;}
    };
    const auto fit_line=[&](double fraction) {
        Line line;
        for(std::size_t i=0;i<count;++i) {
            const auto& s=get(i);
            if(s.end<=s.first || s.end>now+1 || !(s.variance>0) || !std::isfinite(s.variance) ||
               !std::isfinite(s.hz) || (i && s.first<get(i-1).end))return line;
            const auto t=time(s);if(t<-fraction)continue;
            const auto w=1/s.variance;line.m00+=w;line.m01+=w*t;line.m11+=w*t*t;
            line.y0+=w*(s.hz-base);line.y1+=w*t*(s.hz-base);++line.count;
        }
        line.determinant=line.m00*line.m11-line.m01*line.m01;
        if(line.count<8 || !(line.m00>0) || !(line.m11>0) ||
           !(line.determinant>1e-10L*line.m00*line.m11))return line;
        line.hz=static_cast<double>(base+(line.m11*line.y0-line.m01*line.y1)/line.determinant);
        line.slope=static_cast<double>((line.m00*line.y1-line.m01*line.y0)/line.determinant/support);
        double chi=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& s=get(i);const auto t=time(s);if(t<-fraction)continue;
            const auto error=s.hz-line.hz-line.slope*static_cast<double>(t)*support;chi+=error*error/s.variance;
        }
        line.scale=std::max(1.,chi/(line.count-2));
        line.valid=std::isfinite(line.hz) && std::isfinite(line.slope) && std::isfinite(line.scale);
        return line;
    };
    const auto full=fit_line(1.),half=fit_line(.5),quarter=fit_line(.25);
    if(!full.valid || !half.valid || !quarter.valid)return result;
    long double full_variance=0,full_response=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& s=get(i);const auto t=time(s),w=full.weight(t,s.variance);
        full_variance+=w*w*s.variance;full_response+=w*t*t;
    }
    if(!std::isfinite(full_variance) || full_variance<0)return result;
    const auto scale=std::max({full.scale,half.scale,quarter.scale});double bias=0;
    for(unsigned aperture=0;aperture<2;++aperture) {
        const auto fraction=aperture?.25:.5;const auto& recent=aperture?quarter:half;
        long double difference_variance=0,recent_response=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& s=get(i);const auto t=time(s),wf=full.weight(t,s.variance);
            const auto wr=t>=-fraction?recent.weight(t,s.variance):0.L;
            difference_variance+=(wr-wf)*(wr-wf)*s.variance;recent_response+=wr*t*t;
        }
        const auto response_difference=recent_response-full_response;
        if(!std::isfinite(difference_variance) || difference_variance<0 ||
           !(std::abs(response_difference)>1e-10L*(std::abs(full_response)+std::abs(recent_response))))return result;
        // For point-valued segment means under constant curvature, Bfull*a
        // is current bias and (Brecent-Bfull)*a is disagreement. Actual short
        // phase-slope summaries are aperture means; the full physical
        // waveform/late-change checks remain separate from this geometry.
        const auto ratio=std::abs(full_response/response_difference);
        const auto allowance=static_cast<double>(ratio)*(std::abs(recent.hz-full.hz)+
            3*std::sqrt(static_cast<double>(difference_variance)*scale));
        bias=std::max(bias,allowance);
    }
    const auto uncertainty=3*std::sqrt(static_cast<double>(full_variance)*scale)+bias+
        3*sigma/(tau*std::sqrt(support))+.005;
    const auto slope_uncertainty=3*std::sqrt(static_cast<double>(full.m00/full.determinant)*scale)/support;
    if(!std::isfinite(uncertainty) || !std::isfinite(slope_uncertainty) || !std::isfinite(bias))return result;
    return {true,full.hz,full.slope,uncertainty,slope_uncertainty,full.scale,first,count};
}
// Numeric summaries only: no PCM, iterative search or future evolution lease.
struct Runtime {
    std::array<Segment,segment_records> records{};
    std::array<Edge,segment_edges> edges{};
    std::size_t used=0,edge_count=0;std::uint64_t edge_first=0,last_sample=0;
    bool skip_bridge=false;
    void reset()noexcept {used=edge_count=0;edge_first=last_sample=0;skip_bridge=false;}
    void finish(double dt,double sigma,std::uint64_t block)noexcept {
        Segment s;
        if(edge_count && last_sample<=UINT64_MAX-block && make_segment(std::span(edges).first(edge_count),dt,sigma,edge_first,last_sample+block,s)) {
            if(used==records.size()){std::move(records.begin()+1,records.end(),records.begin());--used;}
            records[used++]=s;
        }
        edge_count=0;
    }
    void reject(double dt,double sigma,std::uint64_t block)noexcept {finish(dt,sigma,block);skip_bridge=false;}
    void accept(std::uint64_t sample,std::uint64_t first,Edge e,double dt,double sigma,std::uint64_t block)noexcept {
        if(skip_bridge){skip_bridge=false;return;}
        if(edge_count && last_sample<=UINT64_MAX-block && sample==last_sample+block && e.first_variance!=edges[edge_count-1].last_variance) {
            // This edge shares a physical phase window with the preceding
            // summary. On inconsistent provenance, discard that bridge too.
            finish(dt,sigma,block);return;
        }
        if(edge_count && (last_sample>UINT64_MAX-block || sample!=last_sample+block))finish(dt,sigma,block);
        if(!edge_count)edge_first=first;
        edges[edge_count++]=e;last_sample=sample;
        if(edge_count==edges.size()){finish(dt,sigma,block);skip_bridge=true;}
    }
    Estimate estimate(std::uint64_t now,std::uint32_t rate,double sigma,std::uint64_t block)noexcept {
        const auto width=static_cast<std::uint64_t>(recent_seconds*rate),oldest=now>width?now-width:0;
        std::size_t keep=0;for(std::size_t i=0;i<used;++i)if(records[i].first>=oldest)records[keep++]=records[i];used=keep;
        Segment s;const auto valid=edge_count && edge_first>=oldest && last_sample<=UINT64_MAX-block &&
            make_segment(std::span(edges).first(edge_count),static_cast<double>(block)/rate,sigma,edge_first,last_sample+block,s);
        return pool(std::span(records).first(used),valid?&s:nullptr,now,rate,sigma);
    }
};
}
namespace forecast_detail {
inline constexpr double pilot_balanced_gap_information_fraction=.25,pilot_fallback_frequency_grid_hz=.1;
inline std::size_t pilot_discovery_samples(std::uint32_t sample_rate) noexcept {
    std::size_t n=1024;
    while(n<sample_rate*.16 && n<32768)n*=2;
    return n;
}
// Conditional, deliberately short-aperture pilot model. Frequency summaries
// share a local linear trend, but never phase. The endpoint regression penalty
// prices prediction at the newest dated summary instead of a past mean. Its
// efficiency covers ten20ms Hann projections. The fallback guard allows a
// balanced unknown phase gap in every context: (T/2)^3+(T/2)^3=T^3/4.
// The current fallback's frequency-grid pad is also priced per context.
// These deliberately conservative assumptions are neither calibrated
// confidence nor a physical floor or a promise of acquisition timing.
inline TonePrecisionPrediction pilot_prediction(const TonePrecisionRequest& r) {
    TonePrecisionPrediction result;result.baseline_lead_seconds=r.baseline_lead_seconds;
    const auto steps=static_cast<unsigned>(std::floor(r.max_extra_seconds/.4));
    const auto maximum_extra=.4*steps;
    const auto cn0=std::pow(10.,r.straight_cn0_db_hz/10);
    const auto aperture=r.pilot_coherent_aperture_seconds/r.maximum_source_capture_ratio;
    const auto dwell=.4/r.maximum_source_capture_ratio;
    const auto discovery_samples=pilot_discovery_samples(r.sample_rate);
    const auto seed=static_cast<double>(discovery_samples)/r.sample_rate;
    const auto block=std::max<std::size_t>(16,std::lround(r.sample_rate*.02));
    double window_sum=0,window_square_sum=0;
    for(std::size_t i=0;i<block;++i) {
        const auto w=.5-.5*std::cos(precision_detail::tau*i/(block-1));
        window_sum+=w;window_square_sum+=w*w;
    }
    const auto projection_bandwidth=r.sample_rate*window_square_sum/(window_sum*window_sum);
    // Exact symmetric-Hann ENBW for the actual bounded discovery FFT.
    const auto discovery_bandwidth=1.5*r.sample_rate/(discovery_samples-1);
    const auto bootstrap_cn0=std::max(projection_bandwidth,16*discovery_bandwidth);
    result.coherent_aperture_seconds=aperture;
    result.fallback_information_fraction=pilot_balanced_gap_information_fraction;
    result.fallback_frequency_grid_hz=pilot_fallback_frequency_grid_hz;
    struct Candidate {bool valid=false;double uncertainty=0,ideal=0;std::size_t count=0;};
    const auto candidate=[&](double source_seconds) {
        Candidate c;
        const auto physical=source_seconds/r.maximum_source_capture_ratio;
        // One complete dwell is lost to unknown initial alignment and settling.
        const auto count=std::floor(std::min(36.,std::max(0.,physical-seed-dwell))/dwell);
        if(count<4 || !std::isfinite(cn0) || !(cn0>0))return c;
        c.count=static_cast<std::size_t>(count);
        // Current projection gate1 and discovery gate16 from the production
        // tracker. Failure is a software/bootstrap boundary, not a noise bound.
        if(cn0<=bootstrap_cn0)return c;
        const auto thermal=3/(2*std::numbers::pi*std::numbers::pi*cn0*aperture*aperture*aperture);
        const auto process=2*r.phase_noise_radians_per_sqrt_second*r.phase_noise_radians_per_sqrt_second/
            (precision_detail::tau*precision_detail::tau*aperture);
        // Price the support endpoint, including half a local context of
        // transport beyond the newest context-center frequency date.
        const auto mean_age=.5*((count-1)*dwell+aperture);
        const auto time_square=dwell*dwell*count*(count*count-1)/12;
        const auto endpoint=1/count+mean_age*mean_age/time_square;
        c.ideal=3*std::sqrt(thermal/count);
        const auto guarded_thermal=r.pilot_estimator_efficiency*r.pilot_estimator_efficiency*thermal/
            pilot_balanced_gap_information_fraction;
        const auto grid_variance=pilot_fallback_frequency_grid_hz*pilot_fallback_frequency_grid_hz/9;
        c.uncertainty=3*std::sqrt(endpoint*(guarded_thermal+grid_variance+process));
        c.valid=std::isfinite(c.uncertainty)&&std::isfinite(c.ideal);return c;
    };
    const auto sustained=candidate(r.baseline_lead_seconds+maximum_extra);
    result.sustained_at_seconds=r.baseline_lead_seconds+maximum_extra;
    result.sustained_supported=sustained.valid;
    if(sustained.valid)result.sustained_frequency_half_width_hz=sustained.uncertainty;
    Candidate selected;
    for(unsigned step=0;step<=steps;++step) {
        const auto c=candidate(r.baseline_lead_seconds+.4*step);
        if(!c.valid)continue;
        selected=c;result.extra_lead_seconds=.4*step;
        if(c.uncertainty<=r.target_half_width_hz){result.target_reached=true;break;}
    }
    if(!selected.valid)result.extra_lead_seconds=maximum_extra;
    result.total_lead_seconds=r.baseline_lead_seconds+result.extra_lead_seconds;
    result.extension_capped=!result.target_reached;result.supported=selected.valid;
    if(selected.valid) {
        result.frequency_half_width_hz=selected.uncertainty;
        result.segmented_noise_reference_half_width_hz=selected.ideal;
        result.expected_segments=selected.count;result.usable_tone_seconds=selected.count*aperture;
        result.reason="conditional independent-phase pilot dwell model; local linear frequency trend, short-aperture efficiency and configured Wiener phase noise; no phase fit crosses a retune; conservative balanced-gap/model-selection planning allowance on every context, including 0.1 Hz frequency-grid padding; strong straight fits can be better; measured model competition and excess can widen uncertainty; not calibrated confidence or a physical floor";
    }else {
        result.reason=cn0<=16*discovery_bandwidth?
            "software/bootstrap unsupported: current pilot FFT discovery gate":
            cn0<=projection_bandwidth?"software/bootstrap unsupported: current20ms pilot projection gate":
            "software/bootstrap unsupported: insufficient fresh independent dwell measurements";
        result.reason+="; "+std::to_string(maximum_extra)+
            " s extra-lead cap selected; precision not predicted; target remains unmet";
    }
    result.reason+="; actual discovery, self-interference, retune settling, nonlinear drift and freshness require measured qualification";
    return result;
}
// Cover the entire unknown400ms source calendar, not just the20ms loop
// residue. Each bin certifies clean support for every phase in its interval.
constexpr unsigned source_phase_bins=40;
// Expected clean FLL only. This is the production endpoint covariance algebra
// with zero frequency/evolution residual, NOT an assigned precision or a lock.
// Saved phase endpoints cancel only across genuinely adjacent clean windows.
struct CleanFll {
    std::array<std::uint64_t,384> samples{};
    std::size_t used=0;
    void append(std::uint64_t sample,std::uint64_t now,std::uint32_t rate)noexcept {
        const auto oldest=now>8ULL*rate?now-8ULL*rate:0;
        std::size_t keep=0;
        for(std::size_t i=0;i<used;++i)if(samples[i]>=oldest)samples[keep++]=samples[i];
        used=keep;
        if(used==samples.size()){std::move(samples.begin()+1,samples.end(),samples.begin());--used;}
        samples[used++]=sample;
    }
    bool qualified(std::uint64_t now,std::uint32_t rate,std::uint64_t block)const noexcept {
        return used>=16 && now>=samples[0]-block &&
            static_cast<double>(now-(samples[0]-block)+1)/rate>=.3;
    }
    double uncertainty(std::uint64_t now,std::uint32_t rate,std::uint64_t block,
            double phase_variance,double sigma)const noexcept {
        if(!qualified(now,rate,block))return std::numeric_limits<double>::infinity();
        const auto dt=static_cast<double>(block)/rate;
        const auto endpoint=phase_variance/(precision_detail::tau*precision_detail::tau*dt*dt);
        const auto process=sigma*sigma/(precision_detail::tau*precision_detail::tau*dt);
        double mean=0;
        for(std::size_t i=0;i<used;++i)mean+=(static_cast<long double>(samples[i])-now)/rate;
        mean/=used;double tt=0;
        for(std::size_t i=0;i<used;++i){const auto t=static_cast<double>((static_cast<long double>(samples[i])-now)/rate)-mean;tt+=t*t;}
        if(!(tt>0))return std::numeric_limits<double>::infinity();
        double thermal=0,weights=0,previous=0;
        for(std::size_t i=0;i<used;++i) {
            const auto t=static_cast<double>((static_cast<long double>(samples[i])-now)/rate)-mean;
            const auto w=1./used-mean*t/tt;
            if(i && samples[i]-samples[i-1]==block)thermal+=endpoint*(previous-w)*(previous-w);
            else {if(i)thermal+=endpoint*previous*previous;thermal+=endpoint*w*w;}
            previous=w;weights+=w*w;
        }
        thermal+=endpoint*previous*previous;
        const auto support=static_cast<double>(now-(samples[0]-block)+1)/rate;
        return 3*std::sqrt(thermal+process*weights)+3*sigma/(precision_detail::tau*std::sqrt(std::max(.02,support)))+.005;
    }
};
inline bool clean_window(std::uint64_t first,std::uint64_t block,
        const TonePrecisionRequest& r,double phase_begin,double phase_end)noexcept {
    const auto begin=static_cast<double>(first)/r.sample_rate*r.maximum_source_capture_ratio+phase_begin;
    const auto end=static_cast<double>(first+block-1)/r.sample_rate*r.maximum_source_capture_ratio+phase_end;
    const auto dwell=std::floor(begin/.4),lo=begin-.4*dwell,hi=end-.4*dwell;
    const auto metadata_start=r.metadata_symbols?.388-.008*r.metadata_symbols:.396;
    // Actual paired280ms waveform: both 8ms transitions interrupt phase.
    // The inverted32ms plateau cannot by itself create a200ms context.
    const auto punctuation_clean=!r.punctuate ||
        ((hi<=.236 || lo>=.244) && (hi<=.276 || lo>=.284));
    return hi<.4 && lo>=.004 && hi<=metadata_start && punctuation_clean;
}
struct ExpectedAperture {
    phase_detail::Runtime phase;
    CleanFll fll;
    std::uint64_t first=8192,last_good=0; // two actual fresh4096-point FFTs
    std::size_t capture_used=0;
    bool capture_pending=true,previous_clean=false;
    void advance(std::uint64_t end,const TonePrecisionRequest& r,std::uint64_t block,
            double phase_variance,double phase_begin,double phase_end)noexcept {
        while(first<=end && block<=end-first) {
            const auto next=first+block,now=next-1;
            const auto clean=clean_window(first,block,r,phase_begin,phase_end);
            bool accepted=false;
            if(capture_pending) {
                if(clean && previous_clean)++capture_used;else capture_used=0;
                if(capture_used==4){capture_pending=false;capture_used=0;previous_clean=false;}
                else previous_clean=clean;
            }else {
                accepted=clean && previous_clean;previous_clean=clean;
                if(accepted)fll.append(first,now,r.sample_rate);
            }
            const auto qualified=accepted && fll.qualified(now,r.sample_rate,block);
            if(qualified)last_good=next;
            // A joint context consumes RAW normalized complex rows, not ten
            // individually accepted phase estimates. Its unknown amplitude
            // and residual-frequency nuisance use the shared joint_context
            // covariance. Known dirty forecast rows break the expected clean
            // context; actual runtime purity remains an independent test.
            auto fll_U=std::numeric_limits<double>::infinity();
            if(qualified && phase.context_used>=phase_detail::context_size-1) {
                const auto start=phase.context[phase.context_used==phase_detail::context_size?1:0].first;
                if(!phase.used || start>=phase.anchors[phase.used-1].end)
                    fll_U=fll.uncertainty(now,r.sample_rate,block,phase_variance,r.phase_noise_radians_per_sqrt_second);
            }
            const phase_detail::Projection p{first,next,1,8192,r.sample_rate,0,phase_variance,
                0,fll_U,0,0,clean,accepted,qualified,{1,0},clean?2*phase_variance:0};
            phase.observe(p,r.phase_noise_radians_per_sqrt_second);
            first=next;
        }
    }
    phase_detail::Estimate estimate(std::uint64_t end,const TonePrecisionRequest& r,
            double& usable)const noexcept {
        usable=0;
        // Forecast a dated clean measurement, never relabel the dirty tail or
        // promise lock at the lead boundary. The actual runtime remains free
        // to reject its seed, purity, cycle/model tests or current input.
        if(!last_good || end<last_good || static_cast<double>(end-last_good)/r.sample_rate>.2)return {};
        const auto horizon=36ULL*r.sample_rate;
        const auto oldest=last_good>horizon?last_good-horizon:0;
        std::size_t begin=0;
        while(begin<phase.used && phase.anchors[begin].first<oldest)++begin;
        const auto anchors=std::span(phase.anchors).subspan(begin,phase.used-begin);
        for(const auto& a:anchors) {
            if(a.end>last_good || a.end<=a.first)return {};
            usable+=static_cast<double>(a.end-a.first)/r.sample_rate;
        }
        return phase_detail::estimate(anchors,static_cast<double>(last_good-1-8192)/r.sample_rate,
            r.phase_noise_radians_per_sqrt_second,0);
    }
};
}
// Conditional sampled precision using the SAME dated phase-anchor GLS kernel
// as runtime. Expected clean joint contexts and zero excess are assumptions;
// this does not qualify an input, predict its lock date, or create phase/clock
// authority. The independent current FLL interval must resolve every cycle.
inline TonePrecisionPrediction predict_tone_precision(const TonePrecisionRequest& r) {
    TonePrecisionPrediction result;result.baseline_lead_seconds=r.baseline_lead_seconds;
    if(r.sample_rate<1000 || r.sample_rate>192000 || !std::isfinite(r.straight_cn0_db_hz) ||
       !std::isfinite(r.phase_noise_radians_per_sqrt_second) || r.phase_noise_radians_per_sqrt_second<0 ||
       !std::isfinite(r.baseline_lead_seconds) || r.baseline_lead_seconds<0 ||
       !std::isfinite(r.target_half_width_hz) || r.target_half_width_hz<=0 ||
       !std::isfinite(r.max_extra_seconds) || r.max_extra_seconds<0 || r.max_extra_seconds>36 ||
       !std::isfinite(r.maximum_source_capture_ratio) || r.maximum_source_capture_ratio<=0 ||
       !std::isfinite(r.maximum_tracking_slew_hz_per_second) || r.maximum_tracking_slew_hz_per_second<0 ||
       !std::isfinite(r.pilot_coherent_aperture_seconds) || r.pilot_coherent_aperture_seconds<=0 || r.pilot_coherent_aperture_seconds>.392 ||
       !std::isfinite(r.pilot_estimator_efficiency) || r.pilot_estimator_efficiency<1 ||
       r.metadata_symbols>16) {result.reason="invalid precision request";return result;}
    const auto maximum_total=static_cast<long double>(r.baseline_lead_seconds)+r.max_extra_seconds;
    const auto maximum_physical=maximum_total/r.maximum_source_capture_ratio;
    if(maximum_total>300 || !std::isfinite(maximum_physical) || maximum_physical>300) {
        result.reason="precision forecast exceeds bounded300s source/capture domain";return result;
    }
    if(r.pilot_segmented)return forecast_detail::pilot_prediction(r);
    auto max_steps=static_cast<unsigned>(std::floor(r.max_extra_seconds/.4));
    while(max_steps && .4*max_steps>r.max_extra_seconds)--max_steps;
    const auto maximum_extra=.4*max_steps;
    const auto capped_without_precision=[&](const char* reason) {
        TonePrecisionPrediction capped;
        capped.baseline_lead_seconds=r.baseline_lead_seconds;capped.extra_lead_seconds=maximum_extra;
        capped.total_lead_seconds=r.baseline_lead_seconds+maximum_extra;capped.extension_capped=true;
        capped.sustained_at_seconds=capped.total_lead_seconds;
        capped.reason=std::string(reason)+"; "+std::to_string(maximum_extra)+
            " s extra-lead cap selected; precision not predicted; target remains unmet";
        return capped;
    };
    const auto block=std::clamp<std::size_t>(std::lround(r.sample_rate*.02),16,4096);
    const auto dt=static_cast<double>(block)/r.sample_rate;double ws=0,w2=0;
    for(std::size_t i=0;i<block;++i){const auto w=.5-.5*std::cos(precision_detail::tau*i/(block-1));ws+=w;w2+=w*w;}
    const auto bw=r.sample_rate*w2/(ws*ws),cn0=std::pow(10.,r.straight_cn0_db_hz/10);
    if(!std::isfinite(cn0)) {result.reason="expected straight tone exceeds the numerical precision range";return result;}
    if(cn0<=4*bw)return capped_without_precision("software unsupported: expected straight tone is below the current independent20ms FLL gate; not a physical precision limit");
    if(!(2.*r.sample_rate/4096 < .5/dt))return capped_without_precision(
        "original4096-point seed interval does not establish unique20ms phase cycles");
    const auto phase_variance=bw/(2*cn0);
    struct Candidate {bool valid=true;double U=0,usable=std::numeric_limits<double>::infinity();std::size_t count=SIZE_MAX;};
    std::array<Candidate,91> candidates{};
    // Build each source-phase interval ONCE. The earliest possible window
    // start and latest possible end must both be inside the SAME clean
    // calendar interval, covering every phase within each10ms bin. Ten ms
    // leaves the232ms pre-punctuation CW flank enough room for a200ms
    // context plus one20ms grid placement;20ms-wide bins need not do so.
    // No PCM, FFT, heap history, or repeated aperture replay.
    for(unsigned bin=0;bin<forecast_detail::source_phase_bins;++bin) {
        const auto phase_begin=.4*bin/forecast_detail::source_phase_bins;
        const auto phase_end=.4*(bin+1)/forecast_detail::source_phase_bins;
        forecast_detail::ExpectedAperture aperture;
        for(unsigned step=0;step<=max_steps;++step) {
            const auto total=r.baseline_lead_seconds+.4*step;
            const auto end=static_cast<std::uint64_t>(total/r.maximum_source_capture_ratio*r.sample_rate);
            aperture.advance(end,r,block,phase_variance,phase_begin,phase_end);
            double usable=0;const auto estimate=aperture.estimate(end,r,usable);
            auto& c=candidates[step];c.valid&=estimate.valid;
            if(estimate.valid){c.U=std::max(c.U,estimate.uncertainty);c.usable=std::min(c.usable,usable);c.count=std::min(c.count,estimate.count);}
        }
    }
    // The final bounded aperture was already evaluated above. Expose it
    // separately rather than selecting more TX lead or pricing all future
    // receiver work at the initial target-crossing width.
    const auto& sustained=candidates[max_steps];
    result.sustained_at_seconds=r.baseline_lead_seconds+maximum_extra;
    result.sustained_supported=sustained.valid;
    if(sustained.valid)result.sustained_frequency_half_width_hz=sustained.U;
    for(unsigned step=0;step<=max_steps;++step) {
        const auto& c=candidates[step];if(!c.valid)continue;
        result.supported=true;result.extra_lead_seconds=.4*step;result.total_lead_seconds=r.baseline_lead_seconds+result.extra_lead_seconds;
        result.frequency_half_width_hz=c.U;result.usable_tone_seconds=c.usable;result.expected_segments=c.count;
        result.target_reached=c.U<=r.target_half_width_hz;if(result.target_reached)break;
    }
    if(!result.supported || (!result.target_reached && result.extra_lead_seconds!=maximum_extra))return capped_without_precision(
        "insufficient expected fresh unambiguous phase anchors at the extra-lead cap");
    result.extension_capped=!result.target_reached;
    result.reason=result.target_reached?
        "initial conditional all-source-phase clean-model200ms anchor target predicted; not noisy-waveform precision or measured lock timing":
        "extra lead cap reached; initial conditional clean-model phase-anchor target remains unmet";
    result.reason+="; actual seed, purity, excess, cycle, model and freshness qualification remain independent";
    if(result.sustained_supported)result.reason+="; separate sustained clean-model prediction uses the bounded aperture and adds no transmit lead";
    return result;
}
}
