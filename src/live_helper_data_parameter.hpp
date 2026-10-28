#pragma once
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/symbol_schedule.hpp"
#include "live_helper_constructor_radius.hpp"
#include <numeric>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace datapump::live::detail {
// A conditional oscillator/source PARAMETER lease. It is not a clean-output,
// presence, phase, UTC, scheduled-null or END certificate.
struct DataParameterStamp {
    std::uint64_t provider=0,epoch=0;
    bool valid()const noexcept{return provider&&epoch;}
    friend bool operator==(const DataParameterStamp&,const DataParameterStamp&)=default;
};
struct DataParameterBound {
    enum class Kind:std::uint8_t {unsupported,bounded,retune_nuisance};
    double lower=0,upper=0;
    DataParameterStamp stamp;
    Kind kind=Kind::unsupported;
    bool includes_nuisance=false;
    static double down(long double value)noexcept {auto out=static_cast<double>(value);if(static_cast<long double>(out)>value)out=std::nextafter(out,-INFINITY);return out;}
    static double up(long double value)noexcept {auto out=static_cast<double>(value);if(static_cast<long double>(out)<value)out=std::nextafter(out,INFINITY);return out;}
    static DataParameterBound around(double mean,double radius,double q,DataParameterStamp stamp)noexcept {
        return {down(static_cast<long double>(mean)-radius+q),up(static_cast<long double>(mean)+radius+q),stamp,Kind::bounded,false};
    }
    bool supported()const noexcept{return stamp.valid()&&kind!=Kind::unsupported&&
        (kind!=Kind::bounded||(std::isfinite(lower)&&std::isfinite(upper)&&lower<=upper));}
    static DataParameterBound join(const DataParameterBound& a,const DataParameterBound& b)noexcept {
        if(!a.supported()||!b.supported()||a.stamp!=b.stamp)return {};
        auto out=a;out.includes_nuisance=a.includes_nuisance||b.includes_nuisance||
            a.kind==Kind::retune_nuisance||b.kind==Kind::retune_nuisance;
        if(b.kind==Kind::bounded){
            if(a.kind==Kind::bounded){out.lower=std::min(a.lower,b.lower);out.upper=std::max(a.upper,b.upper);}
            else {out.lower=b.lower;out.upper=b.upper;out.kind=Kind::bounded;}
        }
        return out;
    }
};
struct DataParameterHull:DataParameterBound {std::uint64_t first=0,end=0;};
class DataParameterHistory {
    struct Entry {std::uint64_t first=0,end=0;DataParameterBound bound;};
    std::array<Entry,512> rows_{};
    std::size_t count_=0;
    std::uint64_t next_=0;
    bool dated_=false;
    void piece(Entry row)noexcept {
        if(count_==rows_.size()){
            auto& a=rows_[0];const auto& b=rows_[1];
            a.bound=a.end==b.first?DataParameterBound::join(a.bound,b.bound):DataParameterBound{};a.end=b.end;
            for(std::size_t i=2;i<count_;++i)rows_[i-1]=rows_[i];--count_;
        }
        rows_[count_++]=row;
    }
public:
    bool append(std::uint64_t first,std::uint64_t end,DataParameterBound bound)noexcept {
        if(first>=end||(dated_&&first<next_))return false;
        if(dated_&&first>next_)piece({next_,first,{}});
        piece({first,end,bound});next_=end;dated_=true;return true;
    }
    DataParameterHull query(std::uint64_t first,std::uint64_t end,DataParameterStamp expected)const noexcept {
        DataParameterHull out;if(!expected.valid()||!count_||first>=end||first<rows_[0].first||end>next_)return out;
        auto at=first;bool have=false;
        for(std::size_t i=0;i<count_&&at<end;++i){const auto& row=rows_[i];if(row.end<=at)continue;
            if(row.first>at||!row.bound.supported()||row.bound.stamp!=expected)return {};
            static_cast<DataParameterBound&>(out)=have?DataParameterBound::join(out,row.bound):row.bound;
            if(!out.supported())return {};if(!have)out.first=row.first;out.end=row.end;have=true;at=std::min(end,row.end);
        }
        if(!have||at!=end||out.kind!=DataParameterBound::Kind::bounded)return {};
        return out;
    }
    void discard_before(std::uint64_t sample)noexcept {std::size_t n=0;while(n<count_&&rows_[n].end<=sample)++n;
        for(std::size_t i=n;i<count_;++i)rows_[i-n]=rows_[i];count_-=n;}
};
static_assert(sizeof(DataParameterHistory)<=65536);
struct DataParameterConstructor {
    bool valid=false;
    double lower=0,upper=0,data_hz=0,utc_limit=0;
    std::uint32_t rate=0;
    std::size_t pair_count=0;
    std::uint64_t trace_second=UINT64_MAX;
    DataParameterStamp stamp;
    std::optional<modem::OscillatorSearchConfig> oscillator;
    bool contains(const DataParameterHull& h,const modem::Config& c,double utc,DataParameterStamp current)const noexcept {
        return valid&&stamp==current&&h.stamp==current&&h.kind==DataParameterBound::Kind::bounded&&h.supported()&&
            c.sample_rate==rate&&c.carrier_hz==data_hz&&utc==utc_limit&&c.oscillator_search==oscillator&&
            lower<=h.lower&&h.upper<=upper;
    }
};
// Explicit DATA-coordinate rectangle. K=0 preserves the independently resolved
// COMPLETE original LF/UTC domain; it does not measure or prune that domain.
inline std::optional<DataParameterConstructor> data_parameter_search(const modem::Config& config,
        double utc,const DataParameterBound& bound,modem::PatternSearch& search,double capture_low,double capture_high) {
    if(!config.oscillator_search||!bound.supported()||bound.kind!=DataParameterBound::Kind::bounded)return {};
    // Split scheduled symbols have a separately extended physical aperture
    // and refined RF/clock lattice. Do not substitute this ordinary-symbol
    // headroom policy for their required scheduled-complex cell proof.
    if(modem::scheduled_bursts(config)&&!modem::packed_scheduled_symbols(config))return {};
    const auto center=std::midpoint(bound.lower,bound.upper);
    const auto measured_radius=DataParameterBound::up(std::max(static_cast<long double>(center)-bound.lower,static_cast<long double>(bound.upper)-center));
    // One ADDITIONAL ordinary RF CELL RADIUS funds constructor headroom.
    // This changes actual templates, NOT the measured interval or uncertainty.
    // The value is a cheap headroom policy for native/unsplit compact symbols,
    // not a claim that this query certifies compact execution or sensitivity.
    // Every RF/clock template still comes from the official complete lattice.
    const auto cell_radius=modem::native_pattern_frequency_cell_radius(config);
    const auto radius=helper_constructor_outward_add(measured_radius,cell_radius);
    if(!std::isfinite(center)||!std::isfinite(radius)||radius<0)return {};
    auto c=config;auto& p=*c.oscillator_search;
    const auto original_lf=p.reference==modem::OscillatorReference::shared_radio&&p.rf_shift_hz!=0?p.rf.accuracy_ppm:p.lf.accuracy_ppm;
    p.reference=modem::OscillatorReference::independent_audio;p.lf.accuracy_ppm=original_lf;
    if(p.rf_shift_hz==0)p.rf_shift_hz=1;
    // This is a search-local unit conversion, not a new oscillator prior.
    // Round outward before the ordinary catalogue converts ppm back to Hz;
    // inward rounding can otherwise decline a valid closed DATA interval.
    p.rf.accuracy_ppm=DataParameterBound::up(static_cast<long double>(radius)*1e6L/
        (static_cast<long double>(p.rf_shift_hz)*p.margin));
    modem::PatternSearchResolution resolution;resolution.clock_frequency_hz=0;
    auto catalogue=modem::oscillator_pattern_search(c,utc,resolution,modem::PatternUtcClockGrid::complete_expanded_domain);
    if(catalogue.limited||catalogue.frequency.limited||catalogue.timing_correction_unavailable||
       catalogue.clock_half_width_ppm<catalogue.requested_clock_half_width_ppm||catalogue.hypotheses.empty())return {};
    const auto half=modem::pattern_pulse_enabled(config)?
        (1+modem::pattern_pulse_rolloff)*config.sample_rate/(2.*modem::pattern_chip_samples(config)):
        modem::waveform_bandwidth_hz(config)/2;
    const auto clock=catalogue.requested_clock_half_width_ppm*1e-6;
    const auto support=static_cast<long double>(half)*(1+clock);
    if(!std::isfinite(capture_low)||!std::isfinite(capture_high)||capture_low<0||capture_low>=capture_high||
       capture_high>config.sample_rate/2.)return {};
    double lo=INFINITY,hi=-INFINITY;
    for(auto& pair:catalogue.hypotheses){pair.frequency_offset_hz+=center;
        if(!std::isfinite(pair.frequency_offset_hz))return {};lo=std::min(lo,pair.frequency_offset_hz);hi=std::max(hi,pair.frequency_offset_hz);}
    // Check the FULL shifted DATA rectangle at LF-domain endpoints, including
    // waveform dilation, against the immutable physical/filter output union.
    if(static_cast<long double>(config.carrier_hz)+lo-support<capture_low||
       static_cast<long double>(config.carrier_hz)+hi+support>capture_high)return {};
    // Actual representatives reach both requested boundaries. Core ordinary
    // cell coverage/passband checks remain in force at construction.
    if(lo>bound.lower||hi<bound.upper)return {};
    const auto pair_count=catalogue.hypotheses.size();
    search.qualified_clock_interval.reset();
    search.hypotheses=std::move(catalogue.hypotheses);search.frequency_offsets_hz.clear();search.couple_clock_to_carrier=false;
    // The shifted rectangle is not a complete zero-centered symmetric prior.
    // The typed receipt is authoritative; omit that legacy symmetric claim.
    search.admitted_frequency_half_width_hz.reset();
    search.admitted_clock_half_width_ppm=catalogue.requested_clock_half_width_ppm;
    search.admitted_clock_cell_half_width_ppm=catalogue.clock_step_ppm/2;
    search.complex_frequency_coverage=true;search.complex_clock_coverage=true;
    DataParameterConstructor receipt;receipt.valid=true;receipt.lower=lo;receipt.upper=hi;receipt.data_hz=config.carrier_hz;
    receipt.utc_limit=utc;receipt.rate=config.sample_rate;receipt.pair_count=pair_count;receipt.stamp=bound.stamp;receipt.oscillator=config.oscillator_search;return receipt;
}
}
