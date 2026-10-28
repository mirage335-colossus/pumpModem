#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <climits>
#include <optional>
#include <memory>
#include <numeric>
#include "datapump/modem.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/helper_tone_tracker.hpp"

namespace datapump::live::detail {
struct HelperInputView;
inline bool helper_score_same_view(const std::weak_ptr<const HelperInputView>& previous,
        const std::shared_ptr<const HelperInputView>& current)noexcept {
    return current && previous.lock()==current;
}
struct HelperFrequencyDomainStamp {
    std::uint64_t provider_generation=0,model_generation=0;
    constexpr bool valid()const noexcept{return provider_generation&&model_generation;}
    friend constexpr bool operator==(const HelperFrequencyDomainStamp&,const HelperFrequencyDomainStamp&)=default;
};
// Caller supplies an already justified corrected Helper-M bound. This numeric
// history cannot certify physical filter support, presence, phase or scoring.
struct HelperScoreFrequencyCertificate {
    std::uint64_t first_sample=0,end_sample=0;
    double lower_hz=0,upper_hz=0;
    HelperFrequencyDomainStamp stamp;
    bool signal_supported=false;
};
struct HelperScoreFrequencyHull {
    bool valid=false;
    std::uint64_t support_first_sample=0,support_end_sample=0;
    double lower_hz=0,upper_hz=0;
    HelperFrequencyDomainStamp stamp;
};
class HelperScoreFrequencyHistory {
    struct Bucket {
        std::uint64_t first=0,end=0;
        double lower=0,upper=0;
        HelperFrequencyDomainStamp stamp;
        bool unsupported=false,compatible=true,has_signal=false,single_second=true;
    };
public:
    static constexpr std::size_t capacity=512;
    static constexpr std::size_t storage_required()noexcept{return sizeof(HelperScoreFrequencyHistory);}
    bool configure(std::uint32_t rate)noexcept {
        if(!rate||rate_)return false;
        rate_=rate;return true;
    }
    std::uint32_t sample_rate()const noexcept{return rate_;}
    std::size_t size()const noexcept{return count_;}
    void invalidate_model()noexcept {
        for(std::size_t i=0;i<count_;++i){buckets_[i].unsupported=true;buckets_[i].compatible=false;}
    }
    bool append(const HelperScoreFrequencyCertificate& c)noexcept {
        if(!rate_||c.first_sample>=c.end_sample||(have_dates_&&c.first_sample<next_sample_))return false;
        if(have_dates_&&next_sample_<c.first_sample){
            Bucket gap;gap.unsupported=true;gap.compatible=false;
            append_span(next_sample_,c.first_sample,gap);
        }
        Bucket value;value.stamp=c.stamp;value.compatible=c.stamp.valid();
        const auto finite_bound=std::isfinite(c.lower_hz)&&std::isfinite(c.upper_hz)&&c.lower_hz<=c.upper_hz;
        value.has_signal=c.signal_supported&&finite_bound&&value.compatible;
        value.unsupported=!value.has_signal;
        if(value.has_signal){value.lower=c.lower_hz;value.upper=c.upper_hz;}
        append_span(c.first_sample,c.end_sample,value);
        next_sample_=c.end_sample;have_dates_=true;return true;
    }
    HelperScoreFrequencyHull query(std::uint64_t first,std::uint64_t end)const noexcept {
        HelperScoreFrequencyHull out;
        if(!rate_||!count_||first>=end||first<buckets_[0].first||end>next_sample_)return out;
        auto cursor=first;bool included=false,signal=false;
        for(std::size_t i=0;i<count_&&cursor<end;++i){
            const auto& b=buckets_[i];if(b.end<=cursor)continue;
            if(b.first>cursor||b.unsupported||!b.compatible||!b.stamp.valid())return {};
            if(!included){out.support_first_sample=b.first;out.stamp=b.stamp;included=true;}
            else if(out.stamp!=b.stamp)return {};
            out.support_end_sample=b.end;
            if(b.has_signal){
                if(signal){out.lower_hz=std::min(out.lower_hz,b.lower);out.upper_hz=std::max(out.upper_hz,b.upper);}
                else {out.lower_hz=b.lower;out.upper_hz=b.upper;signal=true;}
            }
            cursor=std::min(end,b.end);
        }
        out.valid=included&&signal&&cursor==end;return out;
    }
    // Only the caller's independently proven watermark authorizes retirement.
    // Keep an intersected bucket whole, even when its coarse hull is old/wide.
    void discard_before(std::uint64_t watermark)noexcept {
        std::size_t drop=0;while(drop<count_&&buckets_[drop].end<=watermark)++drop;
        if(!drop)return;
        for(std::size_t i=drop;i<count_;++i)buckets_[i-drop]=buckets_[i];
        count_-=drop;
    }
private:
    static Bucket merged(const Bucket& a,const Bucket& b)noexcept {
        auto out=a;out.end=b.end;out.single_second=false;
        out.unsupported=a.unsupported||b.unsupported||a.end!=b.first;
        out.compatible=a.compatible&&b.compatible&&a.stamp==b.stamp;
        if(b.has_signal){
            if(out.has_signal){out.lower=std::min(out.lower,b.lower);out.upper=std::max(out.upper,b.upper);}
            else {out.lower=b.lower;out.upper=b.upper;out.has_signal=true;}
        }
        return out;
    }
    void append_piece(const Bucket& piece)noexcept {
        if(count_){
            auto& last=buckets_[count_-1];
            if(last.end==piece.first&&last.single_second&&piece.single_second&&last.first/rate_==piece.first/rate_&&
               last.unsupported==piece.unsupported&&last.compatible==piece.compatible&&
               last.has_signal==piece.has_signal&&last.stamp==piece.stamp){
                last=merged(last,piece);last.single_second=true;return;
            }
        }
        if(count_==capacity){
            buckets_[0]=merged(buckets_[0],buckets_[1]);
            for(std::size_t i=2;i<count_;++i)buckets_[i-1]=buckets_[i];
            --count_;
        }
        buckets_[count_++]=piece;
    }
    void append_span(std::uint64_t first,std::uint64_t end,Bucket value)noexcept {
        const auto retained_tail=static_cast<std::uint64_t>(capacity-1)*rate_;
        if(end-first>retained_tail+rate_) {
            auto boundary=end-retained_tail;boundary-=boundary%rate_;
            if(boundary>first) {
                value.first=first;value.end=boundary;value.single_second=false;append_piece(value);
                first=boundary;value.single_second=true;
            }
        }
        while(first<end){
            // Integer subtraction before addition also works near UINT64_MAX.
            const auto count=std::min<std::uint64_t>(end-first,rate_-first%rate_);
            value.first=first;value.end=first+count;append_piece(value);first=value.end;
        }
    }
    std::array<Bucket,capacity> buckets_{};
    std::uint64_t next_sample_=0;
    std::size_t count_=0;
    std::uint32_t rate_=0;
    bool have_dates_=false;
};
static_assert(HelperScoreFrequencyHistory::storage_required()<=65536);


// Center/radius enclosure of an ASYMMETRIC corrected-M interval. The ordinary
// clock selector still adds the complete existing(H-D)*B term itself.
inline std::optional<std::array<double,2>> helper_score_center_radius(double lo,double hi)noexcept {
    if(!std::isfinite(lo)||!std::isfinite(hi)||lo>hi)return {};
    const auto center=std::midpoint(lo,hi);
    const auto radius=std::max(static_cast<long double>(center)-lo,static_cast<long double>(hi)-center);
    auto r=static_cast<double>(radius);
    if(static_cast<long double>(r)<radius)r=std::nextafter(r,INFINITY);
    if(!std::isfinite(center)||!std::isfinite(r)||r<0)return {};
    return std::array{center,r};
}
// A bounded producer ledger. No PCM, receiver pointers, timing/phase authority
// or extrapolation. Recent descriptors remain DISJOINT and uncoalesced until
// their bounded 256-node smoothing opportunity, for at most 4.8 s of raw age;
// sparse observations can shorten that opportunity. Then enter the existing 512
// conservative buckets. Capacity pressure freezes a complete conservative
// certificate rather than granting more authority or retaining PCM. This whole
// object is also the immutable Bank snapshot.
class HelperScoreRefinedHistory {
public:
    static constexpr std::size_t recent_capacity=256;
    struct Diagnostics {
        std::uint64_t publications=0,queries=0,declines=0,replacements=0,widenings=0,recenterings=0;
        std::uint64_t contradictions=0,frozen=0,early_frozen=0,model_boundaries=0;
        std::uint64_t evidence_capture_end=0,last_query_first=0,last_query_end=0,last_evidence_first=0,last_evidence_end=0;
        double previous_lower=0,previous_upper=0,posterior_lower=0,posterior_upper=0;
    } diagnostics;
private:
    struct Recent {
        HelperScoreFrequencyCertificate certificate;
        std::uint64_t raw_first=0,raw_end=0,evidence_first=0,evidence_end=0,evidence_capture_end=0;
        double q_low=0,q_high=0,rejected_lower=0,rejected_upper=0;
        bool eligible=false,veto=false;
    };
    HelperScoreFrequencyHistory frozen_;
    std::array<Recent,recent_capacity> recent_{};
    std::size_t count_=0;
    std::uint32_t raw_rate_=0;
    std::uint64_t next_=0,last_model_sample_=UINT64_MAX,last_context_end_=0,last_model_reset_=0;
    double last_frequency_=0,last_variance_=0,last_rate_variance_=0,last_covariance_=0;
    bool have_dates_=false,have_model_=false;
    void freeze_front(bool early=false)noexcept {
        if(!count_)return;
        frozen_.append(recent_[0].certificate);
        for(std::size_t i=1;i<count_;++i)recent_[i-1]=recent_[i];--count_;
        ++diagnostics.frozen;diagnostics.early_frozen+=early;
    }
    void age(std::uint64_t captured)noexcept {
        while(count_) {
            const auto& r=recent_[0];
            if(r.eligible && !r.veto && (captured<r.raw_first ||
                captured-r.raw_first<=static_cast<std::uint64_t>(raw_rate_)*24/5))break;
            freeze_front();
        }
    }
public:
    bool configure(std::uint32_t logical_rate,std::uint32_t raw_rate=0)noexcept {
        if(!frozen_.configure(logical_rate))return false;
        raw_rate_=raw_rate?raw_rate:logical_rate;return true;
    }
    std::uint32_t sample_rate()const noexcept{return frozen_.sample_rate();}
    std::uint64_t next_sample()const noexcept{return next_;}
    std::size_t size()const noexcept{return frozen_.size()+count_;}
    std::size_t recent_size()const noexcept{return count_;}
    void invalidate_model()noexcept {
        // A rejected model supplies no boundary proving older refinements
        // independent. Retain their dates/numbers, revoke optional authority.
        frozen_.invalidate_model();
        for(std::size_t i=0;i<count_;++i) {
            auto& r=recent_[i];r.certificate.signal_supported=false;r.eligible=false;r.veto=true;
        }
        ++diagnostics.model_boundaries;
    }
    bool append(const HelperScoreFrequencyCertificate& c,std::uint64_t raw_first=0,std::uint64_t raw_end=0,
            double q_low=0,double q_high=0,bool allow_refinement=false,std::uint64_t captured=0)noexcept {
        if(!sample_rate()||c.first_sample>=c.end_sample||(have_dates_&&c.first_sample<next_))return false;
        const auto add=[&](Recent value){if(count_==recent_capacity)freeze_front(true);recent_[count_++]=value;};
        if(have_dates_&&next_<c.first_sample) {
            Recent gap;gap.certificate={next_,c.first_sample,0,0,c.stamp,false};add(gap);
        }
        Recent r;r.certificate=c;r.raw_first=raw_first;r.raw_end=raw_end;r.q_low=q_low;r.q_high=q_high;
        r.evidence_capture_end=captured;
        r.eligible=allow_refinement&&c.signal_supported&&c.stamp.valid()&&raw_first<raw_end&&raw_end<=captured&&
            std::isfinite(c.lower_hz)&&std::isfinite(c.upper_hz)&&c.lower_hz<=c.upper_hz&&
            std::isfinite(q_low)&&std::isfinite(q_high)&&q_low<=q_high;
        add(r);next_=c.end_sample;have_dates_=true;age(captured);return true;
    }
    void publish(const helper::ToneTracker& tracker,const helper::ToneTrackerSnapshot& model,
            double helper_hz,bool nonhop)noexcept {
        const auto captured=model.captured_end_sample;
        // Geometry/continuous-model reset ends the old refinement eligibility;
        // it does NOT invent a provider reset or erase dated executed correction.
        const bool rejected=have_model_&&model.continuous_model_resets!=last_model_reset_;
        if(rejected)invalidate_model();
        if(!nonhop) {
            for(std::size_t i=0;i<count_;++i)recent_[i].eligible=false;
            have_model_=false;
        }
        age(captured);
        if(!nonhop)return;
        const bool changed=!have_model_||rejected||last_model_sample_!=model.sample||last_context_end_!=model.frequency_context_end_sample||
            last_frequency_!=model.frequency_hz||last_variance_!=model.frequency_variance_hz2||
            last_covariance_!=model.frequency_rate_covariance_hz2_per_second||
            last_rate_variance_!=model.frequency_rate_variance_hz2_per_second2;
        if(!changed)return;
        have_model_=true;last_model_sample_=model.sample;last_context_end_=model.frequency_context_end_sample;
        last_model_reset_=model.continuous_model_resets;last_frequency_=model.frequency_hz;
        last_variance_=model.frequency_variance_hz2;last_covariance_=model.frequency_rate_covariance_hz2_per_second;
        last_rate_variance_=model.frequency_rate_variance_hz2_per_second2;
        ++diagnostics.publications;diagnostics.evidence_capture_end=captured;
        for(std::size_t i=0;i<count_;++i) {
            auto& r=recent_[i];if(!r.eligible||r.veto)continue;
            ++diagnostics.queries;const auto q=tracker.frequency_interval(r.raw_first,r.raw_end);
            diagnostics.last_query_first=r.raw_first;diagnostics.last_query_end=r.raw_end;
            if(!q.valid||q.first_sample!=r.raw_first||q.end_sample!=r.raw_end||q.evidence_end_sample>captured||
               q.captured_end_sample!=captured||!std::isfinite(q.frequency_hz)||!std::isfinite(q.uncertainty_hz)||q.uncertainty_hz<0) {
                ++diagnostics.declines;continue;
            }
            const auto down=[](long double x){return std::nextafter(x,-std::numeric_limits<long double>::infinity());};
            const auto up=[](long double x){return std::nextafter(x,std::numeric_limits<long double>::infinity());};
            const auto l=down(down(down(static_cast<long double>(q.frequency_hz)-helper_hz)+r.q_low)-q.uncertainty_hz);
            const auto h=up(up(up(static_cast<long double>(q.frequency_hz)-helper_hz)+r.q_high)+q.uncertainty_hz);
            auto lo=static_cast<double>(l),hi=static_cast<double>(h);
            if(static_cast<long double>(lo)>l)lo=std::nextafter(lo,-INFINITY);
            if(static_cast<long double>(hi)<h)hi=std::nextafter(hi,INFINITY);
            if(!std::isfinite(lo)||!std::isfinite(hi)||lo>hi){++diagnostics.declines;continue;}
            auto& c=r.certificate;
            diagnostics.previous_lower=c.lower_hz;diagnostics.previous_upper=c.upper_hz;
            diagnostics.posterior_lower=lo;diagnostics.posterior_upper=hi;
            diagnostics.last_evidence_first=q.evidence_first_sample;diagnostics.last_evidence_end=q.evidence_end_sample;
            r.evidence_first=q.evidence_first_sample;r.evidence_end=q.evidence_end_sample;r.evidence_capture_end=captured;
            if(hi<c.lower_hz||lo>c.upper_hz) {
                // Retain contradictory posterior for diagnosis. Never intersect
                // it away or promote this descriptor again implicitly.
                r.rejected_lower=lo;r.rejected_upper=hi;r.veto=true;c.signal_supported=false;
                ++diagnostics.contradictions;continue;
            }
            diagnostics.widenings+=(hi-lo>c.upper_hz-c.lower_hz);
            diagnostics.recenterings+=(lo+hi!=c.lower_hz+c.upper_hz);
            c.lower_hz=lo;c.upper_hz=hi;++diagnostics.replacements;
        }
        age(captured);
    }
    HelperScoreFrequencyHull query(std::uint64_t first,std::uint64_t end)const noexcept {
        if(first>=end||!have_dates_||end>next_)return {};
        HelperScoreFrequencyHull out;auto cursor=first;
        const auto absorb=[&](const HelperScoreFrequencyHull& part){
            if(!part.valid)return false;
            if(!out.valid)out=part;
            else {if(out.stamp!=part.stamp)return false;out.lower_hz=std::min(out.lower_hz,part.lower_hz);
                out.upper_hz=std::max(out.upper_hz,part.upper_hz);out.support_end_sample=part.support_end_sample;}
            return true;
        };
        if(!count_||cursor<recent_[0].certificate.first_sample) {
            const auto stop=count_?std::min(end,recent_[0].certificate.first_sample):end;
            if(!absorb(frozen_.query(cursor,stop)))return {};cursor=stop;
        }
        for(std::size_t i=0;i<count_&&cursor<end;++i) {
            const auto& c=recent_[i].certificate;if(c.end_sample<=cursor)continue;
            if(c.first_sample>cursor||!c.signal_supported||!c.stamp.valid()||!std::isfinite(c.lower_hz)||!std::isfinite(c.upper_hz)||c.lower_hz>c.upper_hz)return {};
            if(!absorb({true,c.first_sample,c.end_sample,c.lower_hz,c.upper_hz,c.stamp}))return {};
            cursor=std::min(end,c.end_sample);
        }
        if(cursor!=end)return {};return out;
    }
    void discard_before(std::uint64_t watermark)noexcept {
        frozen_.discard_before(watermark);std::size_t n=0;while(n<count_&&recent_[n].certificate.end_sample<=watermark)++n;
        for(std::size_t i=n;i<count_;++i)recent_[i-n]=recent_[i];count_-=n;
    }
};
static_assert(sizeof(HelperScoreRefinedHistory)<=65536);
struct HelperScoreFrequencyProducer {
    HelperScoreRefinedHistory history;
    std::shared_ptr<const HelperInputView> view;
    std::uint64_t view_generation=0,raw_captured_end=0;
    HelperFrequencyDomainStamp stamp(std::uint64_t provider)const noexcept{return {provider,view_generation};}
};

// No timestamp, phase, END, or physical filter authority. The stamp identifies
// provider and frequency-authority generation; view identity is checked too.
struct HelperScoreFrequencyBasis {
    HelperFrequencyDomainStamp stamp;
    std::uint32_t logical_rate=0;
    double helper_hz=0,data_hz=0,utc_rate_limit=0;
    std::optional<modem::OscillatorSearchConfig> oscillator;
    bool original_prior=false;
    friend bool operator==(const HelperScoreFrequencyBasis&,const HelperScoreFrequencyBasis&)=default;
};
struct HelperScoreFrequencyLease {
    HelperScoreFrequencyBasis basis;
    double center_hz=0,radius_hz=0,lower_hz=0,upper_hz=0;
    std::optional<double> admitted_clock_half_width_ppm,admitted_clock_cell_half_width_ppm;
    std::optional<modem::PatternClockInterval> qualified_clock_interval;
    bool valid=false,complete_original=false,manual_scalar=false;
    // Joined diagnostic throttling only; constructor coverage above is immutable.
    mutable std::uint64_t trace_second=UINT64_MAX;
    mutable unsigned trace_reason=UINT_MAX;
    // A small unchanged scalar catalogue is also a constructor receipt. Never
    // infer independent clock coverage merely from an absent oscillator model.
    static HelperScoreFrequencyLease freeze_manual(const HelperScoreFrequencyBasis& b,
            const modem::Config& config,const modem::PatternSearch& search,
            const modem::PatternHelperFrequencyWindowState& actual)noexcept {
        HelperScoreFrequencyLease out;out.basis=b;
        const auto zero=[](const std::optional<double>& value){return !value||*value==0;};
        if(b.oscillator||config.oscillator_search||b.original_prior||!b.stamp.valid()||
           b.logical_rate!=config.sample_rate||b.utc_rate_limit!=0||
           !search.hypotheses.empty()||!search.frequency_offsets_hz.empty()||
           !search.expand_clock_search||search.couple_clock_to_carrier||search.uncoupled_frequency_count||
           search.clock_errors_ppm.size()!=1||search.clock_errors_ppm.front()!=0||
           !zero(search.admitted_clock_half_width_ppm)||!zero(search.admitted_clock_cell_half_width_ppm)||
           (search.qualified_clock_interval&&(search.qualified_clock_interval->lower_ppm!=0||
               search.qualified_clock_interval->upper_ppm!=0||search.qualified_clock_interval->cell_half_width_ppm!=0))||
           !actual.supported||!actual.original_pairs||actual.original_pairs>5)return out;
        try {
            // This is the same no-policy geometry used by the core's ordered
            // {0,-step,+step,...} expansion, without allocating another grid.
            const auto geometry=modem::default_pattern_frequency_search(config);
            if(!geometry.count||geometry.count>5||geometry.count!=actual.original_pairs||
               !std::isfinite(geometry.half_width_hz)||geometry.half_width_hz<0)return out;
            const auto lo=-geometry.half_width_hz,hi=geometry.half_width_hz;
            const auto interval=helper_score_center_radius(lo,hi);if(!interval)return out;
            out.lower_hz=lo;out.upper_hz=hi;out.center_hz=(*interval)[0];out.radius_hz=(*interval)[1];
            out.valid=out.manual_scalar=true;return out;
        }catch(...) {return out;} // Optional receipt failure preserves ordinary dispatch.
    }
    static HelperScoreFrequencyLease freeze(const HelperScoreFrequencyBasis& b,double center,double radius,
            const modem::PatternSearch& search,bool executed_complete_original=false)noexcept {
        HelperScoreFrequencyLease out;out.basis=b;out.center_hz=center;out.radius_hz=radius;
        out.admitted_clock_half_width_ppm=search.admitted_clock_half_width_ppm;
        out.admitted_clock_cell_half_width_ppm=search.admitted_clock_cell_half_width_ppm;
        out.qualified_clock_interval=search.qualified_clock_interval;
        if(!b.logical_rate||!b.oscillator||!std::isfinite(center)||!std::isfinite(radius)||radius<0)return out;
        if(executed_complete_original && b.original_prior) {
            out.valid=out.complete_original=true;return out;
        }
        if(!b.stamp.valid()||b.original_prior||executed_complete_original)return out;
        const auto low=static_cast<long double>(center)-radius,high=static_cast<long double>(center)+radius;
        out.lower_hz=static_cast<double>(low);out.upper_hz=static_cast<double>(high);
        if(static_cast<long double>(out.lower_hz)>low)out.lower_hz=std::nextafter(out.lower_hz,-INFINITY);
        if(static_cast<long double>(out.upper_hz)<high)out.upper_hz=std::nextafter(out.upper_hz,INFINITY);
        out.valid=std::isfinite(out.lower_hz)&&std::isfinite(out.upper_hz);return out;
    }
    bool contains(const HelperScoreFrequencyHull& hull,const HelperScoreFrequencyBasis& current)const noexcept {
        auto constructed=basis;constructed.stamp=current.stamp;
        // Caller independently requires the SAME immutable weak view identity.
        // A recreated history may restart its local generation, but every
        // included interval must have that CURRENT history's stamp.
        return valid&&!complete_original&&hull.valid&&constructed==current&&hull.stamp==current.stamp&&
            std::isfinite(hull.lower_hz)&&std::isfinite(hull.upper_hz)&&hull.lower_hz<=hull.upper_hz&&
            lower_hz<=hull.lower_hz&&hull.upper_hz<=upper_hz;
    }
    bool original_basis_matches(HelperScoreFrequencyBasis current)const noexcept {
        auto original=basis;original.stamp={};current.stamp={};
        return valid&&complete_original&&current.original_prior&&original==current;
    }
};
inline std::optional<std::uint64_t> helper_score_global_sample(std::uint64_t bank_origin,
        std::uint64_t row_origin,std::uint64_t local)noexcept {
    if(row_origin>UINT64_MAX-bank_origin)return {};
    const auto origin=bank_origin+row_origin;
    if(local>UINT64_MAX-origin)return {};
    return origin+local;
}
// Translator output n uses ALL raw [n-2*delay,n+1), not only its center.
// A whole chunk may decline at a boundary; PCM and the previous NCO still drain.
inline bool helper_score_hilbert_support(std::uint64_t at,std::uint64_t count,std::uint64_t delay,
        std::uint64_t settled_first,std::uint64_t segment_end)noexcept {
    return count&&delay<=UINT64_MAX/2&&at>=2*delay&&count<=UINT64_MAX-at&&
        settled_first<=at-2*delay&&at+count<=segment_end;
}
}

