#pragma once

#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/symbol_schedule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace datapump::modem::detail {

// Lossless metadata for the ORIGINAL split-symbol acquisition lattice. There
// is no Fit, guard, PCM, or descriptor allocation per origin or logical lane.
// Logical order is pair, original origin index, chronological active phase.
// A qualified prior intersects the original fractional coverage cells; it does
// not translate that lattice. This object neither scores nor drops observations.
//
// The object must remain at a stable address while a Cursor is alive. Each
// scoring/coordinator thread needs its own bound Cursor: source-map queries
// mutate a small calendar cache even when their mathematical input is immutable.
// Zero lanes means an empty metadata domain, never scored absence. A caller
// must not instantiate a receiver or retire required observations from that fact.
class ScheduledAcquisitionCatalog {
public:
    struct Family {
        std::size_t pair=0,origin_first=0,origin_end=0;
        std::size_t range_first=0,range_count=0;
        std::uint64_t phase=0;
        SchedHopCoordinate anchor;
    };
    struct OriginRange {std::size_t first=0,end=0;};
    struct Pair {
        long double rate=1,step=1;
        std::size_t origins=0,frequency=0,rate_index=0,tone_bank_base=0;
        std::size_t family_first=0,family_count=0,lane_first=0,lane_count=0;
        std::size_t timing_pair=0;
    };
    struct Lane {
        std::size_t id=0,pair=0,origin_index=0,frequency=0,rate_index=0,tone_bank_base=0;
        long double origin=0,rate=1,observed_begin=0;
        std::uint64_t phase_lower=0,phase_upper=0,index=0,first_full_index=0;
        SchedHopCoordinate anchor;
    };
    class Cursor {
    public:
        Cursor(Cursor&&)=default;
        Cursor& operator=(Cursor&&)=default;
        Cursor(const Cursor&)=delete;
        Cursor& operator=(const Cursor&)=delete;
        std::size_t working_bytes()const{return sizeof(*this);}
    private:
        friend class ScheduledAcquisitionCatalog;
        explicit Cursor(const ScheduledAcquisitionCatalog& owner):owner_(&owner),
            map_(*owner.config_.scheduled_hopping,*owner.config_.data_key,owner.config_.sample_rate) {}
        const ScheduledAcquisitionCatalog* owner_;
        SchedHopSourceMap map_;
    };

    ScheduledAcquisitionCatalog(const Config& config,const PatternSearch& search,
            const PatternScheduledComplexGeometry& geometry,
            std::size_t metadata_byte_ceiling=std::numeric_limits<std::size_t>::max())
        :config_(config),qualified_(search.qualified_start_window),
         search_stream_phases_(search.search_stream_phases),phase_step_(geometry.receiver_phase_step_samples),
         timing_subdivisions_(geometry.receiver_timing_subdivisions),
         clock_radius_(geometry.clock_cell_radius_ppm*1e-6L) {
        require(scheduled_bursts(config_)&&!packed_scheduled_symbols(config_)&&config_.data_key&&
            config_.pattern_symbols&&config_.spreading_mode==SpreadingMode::pattern,
            "implicit catalog requires keyed split pattern symbols");
        require(geometry.supported&&phase_step_&&timing_subdivisions_,"implicit catalog requires supported split geometry");
        require(search.start_offset_seconds&&std::isfinite(*search.start_offset_seconds)&&
            std::isfinite(search.start_uncertainty_seconds)&&search.start_uncertainty_seconds>=0,
            "implicit catalog requires finite original arrival bounds");
        require(std::isfinite(clock_radius_)&&clock_radius_>=0,"implicit catalog clock radius is invalid");
        if(qualified_)require(std::isfinite(qualified_->epoch_origin_samples)&&
            std::isfinite(qualified_->phase_scale)&&qualified_->phase_scale>0&&
            std::isfinite(qualified_->half_width_samples)&&qualified_->half_width_samples>=0,
            "implicit catalog qualified map is invalid");
        symbol_=symbol_sample_count(config_);chip_=pattern_chip_samples(config_);
        epoch_sample_=symbol_schedule_detail::multiply(config_.stream_epoch,config_.sample_rate);
        lower_=(static_cast<long double>(*search.start_offset_seconds)-search.start_uncertainty_seconds)*config_.sample_rate;
        upper_=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*config_.sample_rate;
        require(std::isfinite(lower_)&&std::isfinite(upper_)&&std::abs(lower_)<1e15L&&std::abs(upper_)<1e15L,
            "implicit catalog arrival bounds exceed precise coordinates");
        phase_lower_=search.search_stream_phases&&(config_.scramble||config_.dsss)?0:config_.stream_phase_samples;
        phase_upper_=search.search_stream_phases&&(config_.scramble||config_.dsss)?
            (canonical_phase_extent(config_)-1)/phase_step_*phase_step_:config_.stream_phase_samples;
        timing_radius_=lower_<upper_?chip_/(4.L*timing_subdivisions_):0;
        if(qualified_)broad_pad_=64*std::numeric_limits<long double>::epsilon()*
            std::max({1.L,std::abs(lower_),std::abs(upper_),std::abs(qualified_->epoch_origin_samples),qualified_->half_width_samples});
        construction_scratch_bytes_=cursor_reservation_bytes()+sizeof(ActiveRanges);
        account_peak(0,metadata_byte_ceiling);

        // Applications already resolved/refined this authoritative bank before
        // producing geometry. Do not build a second oscillator bank with a
        // different correction policy or hide its allocation in this catalog.
        const auto& hypotheses=search.hypotheses;
        require(!(config_.oscillator_search&&hypotheses.empty()&&search.frequency_offsets_hz.empty()&&
            search.clock_errors_ppm==std::vector<double>{0}&&!search.couple_clock_to_carrier),
            "implicit catalog requires the resolved oscillator bank");
        const bool paired=!hypotheses.empty();
        long double highest_rate=1;
        const auto& rates=search.clock_errors_ppm;
        if(paired) {
            require(hypotheses.size()<=maximum_pattern_frequency_rate_hypotheses,
                "implicit catalog frequency/rate bank exceeds existing limit");
            reserve(frequencies_,hypotheses.size(),metadata_byte_ceiling);
            for(const auto& p:hypotheses) {
                validate_pair(p.frequency_offset_hz,p.clock_error_ppm);
                highest_rate=std::max(highest_rate,1+static_cast<long double>(p.clock_error_ppm)*1e-6L);
                if(std::find(frequencies_.begin(),frequencies_.end(),p.frequency_offset_hz)==frequencies_.end())
                    frequencies_.push_back(p.frequency_offset_hz);
            }
        } else {
            require(!rates.empty()&&rates.size()<=65,"implicit catalog original clock bank is invalid");
            require(search.frequency_offsets_hz.size()<=65,
                "implicit catalog original frequency bank is invalid");
            reserve(frequencies_,search.frequency_offsets_hz.empty()?5:search.frequency_offsets_hz.size(),metadata_byte_ceiling);
            frequencies_.assign(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end());
            if(frequencies_.empty()) {
                const auto step=.25*config_.sample_rate/static_cast<double>(symbol_);
                require(!search.expand_clock_search||default_pattern_frequency_search(config_).count<=5,
                    "implicit catalog expanded carrier bank requires FFT workspace");
                frequencies_={0,-step,step,-2*step,2*step};
            }
            require(!search.couple_clock_to_carrier&&(!search.expand_clock_search||frequencies_.size()<=5)&&
                frequencies_.size()<=65,"implicit catalog coupled carrier bank requires FFT workspace");
            for(const auto ppm:rates) {
                validate_pair(0,ppm);
                highest_rate=std::max(highest_rate,1+static_cast<long double>(ppm)*1e-6L);
            }
        }
        require(frequencies_.size()<=maximum_pattern_frequency_hypotheses,"implicit catalog frequency bank exceeds existing limit");
        const auto offset_limit=paired?pattern_frequency_offset_limit(config_):config_.bandwidth_hz/8;
        for(auto f:frequencies_)require(std::isfinite(f)&&std::abs(f)<=offset_limit,"implicit catalog frequency exceeds occupied band");
        const auto count=paired?hypotheses.size():checked_product(rates.size(),frequencies_.size());
        reserve(pairs_,count,metadata_byte_ceiling);
        const auto original_step=std::max(1.L,std::floor(static_cast<long double>(chip_)/(2*highest_rate)));
        Cursor cursor(*this);
        const auto phases=active_phase_ranges(cursor);
        std::size_t phase_count=0;
        for(const auto& range:phases)phase_count=checked_add(phase_count,(range.second-range.first)/phase_step_+1);
        reserve(families_,checked_product(count,phase_count),metadata_byte_ceiling);
        for(std::size_t p=0;p<count;++p) {
            Pair pair;
            pair.rate_index=paired?p:p/frequencies_.size();
            const auto frequency=paired?hypotheses[p].frequency_offset_hz:frequencies_[p%frequencies_.size()];
            pair.frequency=paired?static_cast<std::size_t>(std::find(frequencies_.begin(),frequencies_.end(),frequency)-frequencies_.begin()):p%frequencies_.size();
            pair.tone_bank_base=checked_product(2,p);
            const auto ppm=paired?hypotheses[p].clock_error_ppm:rates[pair.rate_index];
            pair.rate=1+static_cast<long double>(ppm)*1e-6L;
            require(pair.rate>clock_radius_,"implicit catalog clock cell crosses zero rate");
            pair.step=(paired?static_cast<long double>(chip_)/(2*pair.rate):original_step)/timing_subdivisions_;
            const auto origins=std::ceil((upper_-lower_)/pair.step)+1;
            require(std::isfinite(origins)&&origins>=1&&origins<static_cast<long double>(std::numeric_limits<std::size_t>::max()),
                "implicit catalog origin count overflows");
            pair.origins=static_cast<std::size_t>(origins);pair.family_first=families_.size();pair.lane_first=size_;
            // Timing membership depends on the exact rate, never the carrier
            // label. Keep every original pair/family/ordinal, but share its
            // immutable range runs and keyed source anchors. No approximate
            // rate matching, cross-key reuse or cached admission decision.
            pair.timing_pair=p;
            for(std::size_t previous=0;previous<pairs_.size();++previous)
                if(pairs_[previous].rate==pair.rate && pairs_[previous].step==pair.step &&
                   pairs_[previous].origins==pair.origins){pair.timing_pair=previous;break;}
            if(pair.timing_pair!=p) {
                const auto& shared=pairs_[pair.timing_pair];
                for(auto f=shared.family_first;f<shared.family_first+shared.family_count;++f) {
                    auto family=families_[f];family.pair=p;families_.push_back(family);
                }
                pair.family_count=shared.family_count;pair.lane_count=shared.lane_count;
                size_=checked_add(size_,pair.lane_count);pairs_.push_back(pair);continue;
            }
            for(const auto& range:phases)for(auto phase=range.first;;phase+=phase_step_) {
                Family family;family.pair=p;family.phase=phase;
                family.anchor=cursor.map_.locate(SchedHopCoordinate{epoch_sample_,0}.add_samples(phase)).source;
                family.origin_first=0;family.origin_end=pair.origins;
                family.range_first=ranges_.size();
                if(qualified_) {
                    // Local padding can change at clipped end cells. It need
                    // not define one exactly monotone interval. A CONSTANT
                    // outward pad supplies a monotone superset, then exact
                    // original membership is compressed into contiguous runs.
                    // One outward phase step additionally covers rounding in
                    // the original ceil/floor conversion to integer phases.
                    family.origin_first=first_true(pair.origins,[&](std::size_t i){return qualified_bounds(pair,i,true).second>=static_cast<long double>(phase)-phase_step_;});
                    family.origin_end=first_true(pair.origins,[&](std::size_t i){return qualified_bounds(pair,i,true).first>static_cast<long double>(phase)+phase_step_;});
                    if(family.origin_end<family.origin_first)family.origin_end=family.origin_first;
                    std::size_t run=family.origin_end;
                    for(auto i=family.origin_first;i<family.origin_end;++i) {
                        const bool retained=qualified_phase_retained(pair,i,phase);
                        if(retained&&run==family.origin_end)run=i;
                        if(!retained&&run!=family.origin_end) {
                            append_range({run,i},metadata_byte_ceiling);run=family.origin_end;
                        }
                    }
                    if(run!=family.origin_end)append_range({run,family.origin_end},metadata_byte_ceiling);
                } else {
                    append_range({0,pair.origins},metadata_byte_ceiling);
                }
                family.range_count=ranges_.size()-family.range_first;
                for(std::size_t k=family.range_first;k<family.range_first+family.range_count;++k)
                    pair.lane_count=checked_add(pair.lane_count,ranges_[k].end-ranges_[k].first);
                families_.push_back(family);
                if(phase==range.second)break;
            }
            pair.family_count=families_.size()-pair.family_first;
            size_=checked_add(size_,pair.lane_count);pairs_.push_back(pair);
        }
        account_peak(0,metadata_byte_ceiling);
        // The supplied summary is an independent exact-count contract. Never
        // accept a smaller catalog silently if a caller gave stale geometry.
        require(size_==geometry.retained_hypotheses,"implicit catalog disagrees with original retained lane count");
    }
    ScheduledAcquisitionCatalog(const ScheduledAcquisitionCatalog&)=delete;
    ScheduledAcquisitionCatalog& operator=(const ScheduledAcquisitionCatalog&)=delete;
    ScheduledAcquisitionCatalog(ScheduledAcquisitionCatalog&&)=delete;
    ScheduledAcquisitionCatalog& operator=(ScheduledAcquisitionCatalog&&)=delete;

    Cursor make_cursor()const{return Cursor(*this);}
    static constexpr std::size_t cursor_reservation_bytes(){return sizeof(Cursor)+SchedHopSchedule::frame_bytes+32;}
    std::size_t size()const{return size_;}
    std::size_t working_bytes()const {
        return sizeof(*this)+pairs_.capacity()*sizeof(Pair)+families_.capacity()*sizeof(Family)+frequencies_.capacity()*sizeof(double)+ranges_.capacity()*sizeof(OriginRange);
    }
    std::size_t construction_peak_bytes()const{return construction_peak_bytes_;}
    const std::vector<Pair>& pairs()const{return pairs_;}
    const std::vector<Family>& families()const{return families_;}
    const std::vector<OriginRange>& ranges()const{return ranges_;}
    const std::vector<double>& frequencies()const{return frequencies_;}
    std::uint64_t phase_step()const{return phase_step_;}
    long double timing_radius()const{return timing_radius_;}
    long double clock_radius()const{return clock_radius_;}
    const Config& config()const{return config_;}

    Lane describe(std::size_t id,Cursor& cursor)const {
        check_cursor(cursor);require(id<size_,"implicit catalog lane index out of range");
        const auto it=std::upper_bound(pairs_.begin(),pairs_.end(),id,
            [](std::size_t index,const Pair& p){return index<p.lane_first+p.lane_count;});
        require(it!=pairs_.end(),"implicit catalog pair index is invalid");
        const auto p=static_cast<std::size_t>(it-pairs_.begin());const auto local=id-it->lane_first;
        const auto origin_end=first_true(it->origins,[&](std::size_t i){return prefix(*it,i+1)>local;});
        require(origin_end<it->origins,"implicit catalog origin rank is invalid");
        auto residual=local-prefix(*it,origin_end);
        for(std::size_t f=it->family_first;f<it->family_first+it->family_count;++f) {
            const auto& family=families_[f];
            if(!contains(family,origin_end))continue;
            if(residual){--residual;continue;}
            return make_lane(id,p,origin_end,family,cursor);
        }
        throw Error("implicit catalog phase rank is invalid");
    }
    std::optional<std::size_t> rank(std::size_t pair,std::size_t origin_index,std::uint64_t phase)const {
        if(pair>=pairs_.size()||origin_index>=pairs_[pair].origins)return std::nullopt;
        const auto& p=pairs_[pair];auto id=checked_add(p.lane_first,prefix(p,origin_index));
        for(std::size_t f=p.family_first;f<p.family_first+p.family_count;++f) {
            const auto& family=families_[f];
            if(!contains(family,origin_index))continue;
            if(family.phase==phase)return id;
            ++id;
        }
        return std::nullopt;
    }
    // Family traversal already knows the pair and phase. Recover its original
    // logical ordinal directly rather than rank() followed by a second binary
    // inversion through the complete lattice. Qualified holes remain checked.
    Lane describe_family(std::size_t family_index,std::size_t origin_index,Cursor& cursor)const {
        check_cursor(cursor);
        require(family_index<families_.size(),"implicit catalog family index out of range");
        const auto& family=families_[family_index];const auto& pair=pairs_[family.pair];
        require(origin_index<pair.origins && contains(family,origin_index),
            "implicit catalog family contains an excluded origin");
        auto id=checked_add(pair.lane_first,prefix(pair,origin_index));
        for(auto f=pair.family_first;f<family_index;++f)
            if(contains(families_[f],origin_index))id=checked_add(id,1);
        return make_lane(id,family.pair,origin_index,family,cursor);
    }
    template<class Consumer> void for_each(std::size_t first,std::size_t count,Cursor& cursor,Consumer consume)const {
        check_cursor(cursor);require(first<=size_&&count<=size_-first,"implicit catalog range out of bounds");
        if(!count)return;
        const auto initial=describe(first,cursor);const auto stop=first+count;
        for(std::size_t p=initial.pair;p<pairs_.size();++p) {
            const auto& pair=pairs_[p];
            auto i=p==initial.pair?initial.origin_index:next_origin(pair,0);
            auto id=pair.lane_first+prefix(pair,i);
            for(;i<pair.origins;i=next_origin(pair,i+1)) {
                for(std::size_t f=pair.family_first;f<pair.family_first+pair.family_count;++f) {
                    const auto& family=families_[f];if(!contains(family,i))continue;
                    if(id>=first)consume(make_lane(id,p,i,family,cursor));
                    if(++id==stop)return;
                }
            }
        }
        throw Error("implicit catalog range traversal ended early");
    }
    // Same scheduled_boundary control arithmetic as the original correlator.
    // END is a waiting fence, not evidence of a scored failed symbol.
    long double boundary(const Lane& lane,long double source,bool end,Cursor& cursor)const {
        check_cursor(cursor);
        const auto physical=cursor.map_.source_to_physical(lane.anchor.add_signed(source),end);
        const auto anchor=SchedHopCoordinate{epoch_sample_,0}.add_samples(lane.phase_lower);
        const auto distance=physical.distance(anchor);
        const auto slow=lane.rate-clock_radius_,fast=lane.rate+clock_radius_;
        auto result=lane.origin+distance/lane.rate;
        if(end)result=lane.origin+timing_radius_/lane.rate+std::max(distance/slow,distance/fast);
        const auto width=search_stream_phases_?std::min(phase_step_-1,canonical_phase_extent(config_)-1-lane.phase_lower):0;
        if(end&&width) {
            const auto upper_anchor=anchor.add_samples(width);
            const auto upper_source=cursor.map_.locate(upper_anchor).source.add_signed(source);
            const auto upper_physical=cursor.map_.source_to_physical(upper_source,true);
            const auto delta=upper_physical.distance(upper_anchor);
            result=std::max(result,lane.origin+timing_radius_/lane.rate+std::max(delta/slow,delta/fast));
        }
        return result;
    }
    std::uint64_t due(const Lane& lane,std::uint64_t index,Cursor& cursor)const {
        const auto source=static_cast<long double>(index)*symbol_+symbol_;
        const auto end=std::ceil(boundary(lane,source,true,cursor));
        require(std::isfinite(end)&&end<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),
            "implicit catalog physical endpoint overflows");
        return static_cast<std::uint64_t>(std::max(0.L,end));
    }
    std::uint64_t initial_due(const Lane& lane,Cursor& cursor)const{return due(lane,lane.index,cursor);}
    std::uint64_t first_full_due(const Lane& lane,Cursor& cursor)const{return due(lane,lane.first_full_index,cursor);}

private:
    static void require(bool ok,const char* message){if(!ok)throw Error(message);}
    static void validate_pair(double f,double ppm) {
        require(std::isfinite(f)&&std::isfinite(ppm)&&std::abs(ppm)<=10000,"implicit catalog frequency/rate pair is invalid");
    }
    static std::size_t checked_add(std::size_t a,std::size_t b) {
        require(b<=std::numeric_limits<std::size_t>::max()-a,"implicit catalog lane count overflows");return a+b;
    }
    static std::size_t checked_product(std::size_t a,std::size_t b) {
        require(!a||b<=std::numeric_limits<std::size_t>::max()/a,"implicit catalog metadata count overflows");return a*b;
    }
    template<class T>void reserve(std::vector<T>& values,std::size_t count,std::size_t limit) {
        const auto bytes=checked_product(count,sizeof(T));
        // reserve may temporarily own both allocations; actual old capacity is
        // already in working_bytes. Constructor-only cursor/ranges are separate.
        account_peak(bytes,limit);
        values.reserve(count);
        account_peak(0,limit);
    }
    void account_peak(std::size_t extra,std::size_t limit) {
        const auto resident=working_bytes();
        require(resident<=limit&&construction_scratch_bytes_<=limit-resident&&
            extra<=limit-resident-construction_scratch_bytes_,"implicit catalog metadata exceeds workspace");
        construction_peak_bytes_=std::max(construction_peak_bytes_,resident+construction_scratch_bytes_+extra);
    }
    template<class Predicate>static std::size_t first_true(std::size_t count,Predicate predicate) {
        std::size_t left=0,right=count;
        while(left<right){const auto mid=left+(right-left)/2;if(predicate(mid))right=mid;else left=mid+1;}
        return left;
    }
    long double origin(const Pair& pair,std::size_t i)const{return std::min(upper_,lower_+static_cast<long double>(i)*pair.step);}
    std::pair<long double,long double> qualified_bounds(const Pair& pair,std::size_t i,bool broad=false)const {
        const auto at=origin(pair,i),previous=i?origin(pair,i-1):lower_;
        const auto next=i+1<pair.origins?origin(pair,i+1):upper_;
        const auto cell_lower=(previous+at)/2,cell_upper=(at+next)/2;
        const auto& q=*qualified_;
        const auto pad=broad?broad_pad_:64*std::numeric_limits<long double>::epsilon()*
            std::max({1.L,std::abs(cell_lower),std::abs(cell_upper),std::abs(q.epoch_origin_samples),q.half_width_samples});
        auto a=(cell_lower-q.epoch_origin_samples-q.half_width_samples-pad)/q.phase_scale;
        const auto b=(cell_upper-q.epoch_origin_samples+q.half_width_samples+pad)/q.phase_scale;
        if(search_stream_phases_)a-=phase_step_-1;
        return {a,b};
    }
    bool qualified_phase_retained(const Pair& pair,std::size_t i,std::uint64_t phase)const {
        auto [a,b]=qualified_bounds(pair,i,false);
        if(a>phase_upper_||b<phase_lower_)return false;
        a=std::clamp(a,static_cast<long double>(phase_lower_),static_cast<long double>(phase_upper_));
        b=std::clamp(b,static_cast<long double>(phase_lower_),static_cast<long double>(phase_upper_));
        const auto first=std::ceil((a-phase_lower_)/phase_step_),last=std::floor((b-phase_lower_)/phase_step_);
        if(first>last)return false;
        const auto lo=phase_lower_+static_cast<std::uint64_t>(first)*phase_step_;
        const auto hi=phase_lower_+static_cast<std::uint64_t>(last)*phase_step_;
        return phase>=lo&&phase<=hi;
    }
    Lane make_lane(std::size_t id,std::size_t p,std::size_t i,const Family& family,Cursor& cursor)const {
        const auto& pair=pairs_[p];Lane lane;lane.id=id;lane.pair=p;lane.origin_index=i;
        lane.origin=origin(pair,i);lane.rate=pair.rate;lane.frequency=pair.frequency;
        lane.rate_index=pair.rate_index;lane.tone_bank_base=pair.tone_bank_base;
        lane.phase_lower=lane.phase_upper=family.phase;lane.anchor=family.anchor;
        const auto tick=SchedHopCoordinate{epoch_sample_,0}.add_samples(family.phase).add_signed(-lane.origin*lane.rate);
        lane.observed_begin=cursor.map_.locate(tick).source.distance(lane.anchor);
        lane.index=source_index(lane.observed_begin,false);lane.first_full_index=source_index(lane.observed_begin,true);
        return lane;
    }
    std::size_t next_origin(const Pair& pair,std::size_t after)const {
        auto next=pair.origins;
        for(std::size_t f=pair.family_first;f<pair.family_first+pair.family_count;++f) {
            const auto& family=families_[f];
            for(std::size_t k=family.range_first;k<family.range_first+family.range_count;++k) {
                const auto& range=ranges_[k];if(after<range.end)next=std::min(next,std::max(after,range.first));
            }
        }
        return next;
    }
    std::size_t prefix(const Pair& pair,std::size_t origins)const {
        std::size_t result=0;
        for(std::size_t f=pair.family_first;f<pair.family_first+pair.family_count;++f) {
            const auto& family=families_[f];
            for(std::size_t k=family.range_first;k<family.range_first+family.range_count;++k) {
                const auto& range=ranges_[k];
                if(origins>range.first)result=checked_add(result,std::min(origins,range.end)-range.first);
            }
        }
        return result;
    }
    bool contains(const Family& family,std::size_t origin)const {
        for(std::size_t k=family.range_first;k<family.range_first+family.range_count;++k)
            if(origin>=ranges_[k].first&&origin<ranges_[k].end)return true;
        return false;
    }
    void append_range(OriginRange range,std::size_t limit) {
        if(range.first==range.end)return;
        if(ranges_.size()==ranges_.capacity()) {
            const auto count=ranges_.capacity()?checked_product(2,ranges_.capacity()):1;
            reserve(ranges_,count,limit);
        }
        ranges_.push_back(range);
    }
    std::uint64_t source_index(long double position,bool full)const {
        const auto index=full?std::ceil(std::max(0.L,position)/symbol_):std::floor(std::max(0.L,position)/symbol_);
        require(index<static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),"implicit catalog source index overflows");
        return static_cast<std::uint64_t>(index);
    }
    void check_cursor(const Cursor& cursor)const{require(cursor.owner_==this,"implicit catalog cursor belongs to another catalog");}
    struct ActiveRanges {
        std::array<std::pair<std::uint64_t,std::uint64_t>,8> values{};
        std::size_t size=0;
        const std::pair<std::uint64_t,std::uint64_t>* begin()const{return values.data();}
        const std::pair<std::uint64_t,std::uint64_t>* end()const{return values.data()+size;}
    };
    ActiveRanges active_phase_ranges(Cursor& cursor)const {
        ActiveRanges ranges;
        auto at=SchedHopCoordinate{epoch_sample_,0}.add_samples(phase_lower_);
        const auto last=SchedHopCoordinate{epoch_sample_,0}.add_samples(phase_upper_);
        while(at.distance(last)<=0) {
            const auto group=cursor.map_.locate(at);
            if(!group.active) {
                const auto next=cursor.map_.source_to_physical(group.source,false);
                require(next.distance(at)>0,"implicit catalog phase inverse did not advance");
                at=next;if(at.distance(last)>0)break;continue;
            }
            const auto low=std::max<std::uint64_t>(phase_lower_,group.physical_begin>epoch_sample_?group.physical_begin-epoch_sample_:0);
            const auto high=std::min<std::uint64_t>(phase_upper_,group.physical_end-1-epoch_sample_);
            const auto first=phase_lower_+(low-phase_lower_+phase_step_-1)/phase_step_*phase_step_;
            const auto end=phase_lower_+(high-phase_lower_)/phase_step_*phase_step_;
            if(first<=end) {
                require(ranges.size<ranges.values.size(),"implicit catalog original active phase range limit exceeded");
                ranges.values[ranges.size++]={first,end};
            }
            at={group.physical_end,0};
        }
        return ranges;
    }
    Config config_;
    std::optional<PatternStartWindow> qualified_;
    bool search_stream_phases_=false;
    std::uint64_t symbol_=0,chip_=0,epoch_sample_=0,phase_step_=1,timing_subdivisions_=1,phase_lower_=0,phase_upper_=0;
    long double lower_=0,upper_=0,timing_radius_=0,clock_radius_=0,broad_pad_=0;
    std::vector<double> frequencies_;
    std::vector<Pair> pairs_;
    std::vector<Family> families_;
    std::vector<OriginRange> ranges_;
    std::size_t size_=0,construction_scratch_bytes_=0,construction_peak_bytes_=0;
};

} // namespace datapump::modem::detail
