#pragma once
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
};
struct TonePrecisionPrediction {
    // supported grants a numeric precision estimate, not measured lock. A valid
    // request can select capped airtime while precision remains unsupported.
    bool supported=false,target_reached=false,extension_capped=false;
    double baseline_lead_seconds=0,extra_lead_seconds=0,total_lead_seconds=0;
    // Numeric precision/support fields are meaningful only when supported;
    // their zero defaults never mean zero measured uncertainty.
    double frequency_half_width_hz=0,usable_tone_seconds=0;
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
// Conditional expected precision from the SAME segment covariance kernel.
// Clean waveform support and zero measured excess are assumptions, not a
// synchronization qualification or a guarantee of achieved receiver precision.
inline TonePrecisionPrediction predict_tone_precision(const TonePrecisionRequest& r) {
    TonePrecisionPrediction result;result.baseline_lead_seconds=r.baseline_lead_seconds;
    if(r.sample_rate<1000 || r.sample_rate>192000 || !std::isfinite(r.straight_cn0_db_hz) ||
       !std::isfinite(r.phase_noise_radians_per_sqrt_second) || r.phase_noise_radians_per_sqrt_second<0 ||
       !std::isfinite(r.baseline_lead_seconds) || r.baseline_lead_seconds<0 ||
       !std::isfinite(r.target_half_width_hz) || r.target_half_width_hz<=0 ||
       !std::isfinite(r.max_extra_seconds) || r.max_extra_seconds<0 || r.max_extra_seconds>36 ||
       !std::isfinite(r.maximum_source_capture_ratio) || r.maximum_source_capture_ratio<=0 ||
       !std::isfinite(r.maximum_tracking_slew_hz_per_second) || r.maximum_tracking_slew_hz_per_second<0 ||
       r.metadata_symbols>16) {result.reason="invalid precision request";return result;}
    // Bound BOTH source/calendar coordinates and simulated captured work
    // before selecting airtime or converting a floating sample coordinate.
    // Invalid geometry must not turn an optional forecast into extra output.
    const auto maximum_total=static_cast<long double>(r.baseline_lead_seconds)+r.max_extra_seconds;
    const auto maximum_physical=maximum_total/r.maximum_source_capture_ratio;
    if(maximum_total>300 || !std::isfinite(maximum_physical) || maximum_physical>300) {
        result.reason="precision forecast exceeds bounded300s source/capture domain";return result;
    }
    auto max_steps=static_cast<unsigned>(std::floor(r.max_extra_seconds/.4));
    while(max_steps && .4*max_steps>r.max_extra_seconds)--max_steps;
    const auto maximum_extra=.4*max_steps;
    const auto capped_without_precision=[&](const char* reason) {
        TonePrecisionPrediction capped;
        capped.baseline_lead_seconds=r.baseline_lead_seconds;
        capped.extra_lead_seconds=maximum_extra;
        capped.total_lead_seconds=r.baseline_lead_seconds+maximum_extra;
        capped.extension_capped=true;
        capped.reason=std::string(reason)+"; "+std::to_string(maximum_extra)+
            " s extra-lead cap selected; precision not predicted; target remains unmet";
        return capped;
    };
    const auto block=std::clamp<std::size_t>(std::lround(r.sample_rate*.02),16,4096);
    const auto dt=static_cast<double>(block)/r.sample_rate;double ws=0,w2=0;
    for(std::size_t i=0;i<block;++i){const auto w=.5-.5*std::cos(precision_detail::tau*i/(block-1));ws+=w;w2+=w*w;}
    const auto bw=r.sample_rate*w2/(ws*ws),cn0=std::pow(10.,r.straight_cn0_db_hz/10);
    if(!std::isfinite(cn0)) {result.reason="expected straight tone exceeds the numerical precision range";return result;}
    if(cn0<=4*bw)return capped_without_precision(
        "expected straight tone is below the independent 20ms following level gate");
    const auto variance=bw/(2*cn0*precision_detail::tau*precision_detail::tau*dt*dt);
    for(unsigned step=0;step<=max_steps;++step) {
        const auto extra=.4*step;if(extra>r.max_extra_seconds || extra>36)break;
        const auto total=r.baseline_lead_seconds+extra;
        double worst=0,min_usable=std::numeric_limits<double>::infinity();std::size_t min_segments=SIZE_MAX;bool all=true;
        // Conditional sampled alignment forecast: evaluate twenty1ms window
        // offsets within the400ms calendar. This is not a universal boundary
        // guarantee; actual capture/partial dwell support is independently
        // measured. Complete windows and known waveform gaps are retained.
        for(unsigned alignment=0;alignment<20;++alignment) {
            precision_detail::Runtime runtime;const auto offset=alignment*.001;
            bool previous_clean=false;std::uint64_t previous_first=0;double usable=0;
            const auto physical=total/r.maximum_source_capture_ratio;
            const auto end=static_cast<std::uint64_t>(physical*r.sample_rate);
            // An expected one-second FFT/cold-loop warmup is excluded. Actual
            // capture still qualifies independently and can take longer.
            for(std::uint64_t first=static_cast<std::uint64_t>(std::ceil(1./dt))*block;first+block<=end;first+=block) {
                const auto source_first=static_cast<double>(first)/r.sample_rate*r.maximum_source_capture_ratio+offset;
                const auto source_last=static_cast<double>(first+block-1)/r.sample_rate*r.maximum_source_capture_ratio+offset;
                const auto dwell=std::floor(source_first/.4),lo=source_first-.4*dwell,hi=source_last-.4*dwell;
                const auto metadata_start=r.metadata_symbols?.388-.008*r.metadata_symbols:.396;
                const auto clean=hi<.4 && lo>=.004 && hi<=metadata_start && (!r.punctuate || hi<=.236 || lo>=.244);
                if(clean) {
                    if(first>= (end>static_cast<std::uint64_t>(precision_detail::recent_seconds*r.sample_rate)?
                       end-static_cast<std::uint64_t>(precision_detail::recent_seconds*r.sample_rate):0))usable+=dt;
                    if(previous_clean)runtime.accept(first,previous_first,{0,variance,variance},dt,r.phase_noise_radians_per_sqrt_second,block);
                }else runtime.reject(dt,r.phase_noise_radians_per_sqrt_second,block);
                previous_clean=clean;previous_first=first;
            }
            if(!end){all=false;break;}
            const auto estimate=runtime.estimate(end-1,r.sample_rate,r.phase_noise_radians_per_sqrt_second,block);
            if(!estimate.valid){all=false;break;}
            worst=std::max(worst,estimate.uncertainty);min_usable=std::min(min_usable,usable);min_segments=std::min(min_segments,estimate.count);
        }
        if(all) {
            result.supported=true;result.total_lead_seconds=total;result.extra_lead_seconds=.4*step;
            result.frequency_half_width_hz=worst;result.usable_tone_seconds=min_usable;result.expected_segments=min_segments;
            result.target_reached=worst<=r.target_half_width_hz;
            if(result.target_reached)break;
        }
    }
    // A fit from a shorter failed-target aperture cannot supply precision at
    // a later unsupported cap. Select the bounded airtime without that U.
    if(!result.supported || (!result.target_reached && result.extra_lead_seconds!=maximum_extra))
        return capped_without_precision("insufficient expected independently captured coherent segments at the extra-lead cap");
    result.extension_capped=result.supported && !result.target_reached;
    result.reason=result.supported?(result.target_reached?"conditional sampled clean-segment/smooth-evolution target reached; actual capture, rejection and excess remain independent":"extra lead cap reached; conditional sampled clean-segment/smooth-evolution target remains unmet"):"insufficient expected independently captured coherent segments";
    return result;
}
}
