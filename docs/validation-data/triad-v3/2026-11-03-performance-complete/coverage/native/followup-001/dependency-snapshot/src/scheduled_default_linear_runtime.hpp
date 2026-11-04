#pragma once
// Private experiment adapter. These point statistics carry NO Original
// numerical certificate and never own admission, carry, timing or diagnostics.
#include "scheduled_default_linear_events.hpp"
#include "scheduled_sparse_owned_spans.hpp"
#include "coarse_pulse_cells.hpp"
#include "pattern_drift.hpp"
#include <array>
#include <span>
#include <vector>
#include <limits>
#include <cmath>

namespace datapump::modem::detail::scheduled_default_linear_runtime {
namespace events=scheduled_default_linear_events;
namespace original=scheduled_original_partition;
namespace sparse=scheduled_sparse_owned_spans;
namespace coarse=scheduled_coarse_pulse_cells;
namespace tile=scheduled_acquisition_tile;
inline constexpr std::size_t maximum_lanes=8,maximum_pieces=8192;
struct Lane {
    std::size_t global=0;
    original::Event event;
    std::vector<original::Piece> pieces;
};
// Keep the FHSS-aware bridge private to this experiment: the sparse colored
// certificate deliberately retains its FHSS-off contract. No old certificate
// or mask proof is enlarged by enabling these native point statistics.
inline bool supported_fhss(const original::Event& e) {
    if(!e.fhss.enabled)return true;
    const auto& f=e.fhss;
    return f.smooth && e.sample_rate && std::isfinite(f.period) && f.period>0 &&
        std::isfinite(f.quiet) && f.quiet>=0 && std::isfinite(f.support) && f.support>=1 &&
        std::isfinite(f.lag_center) && f.lag_center>=0 &&
        f.lag_center<=30.L*e.sample_rate && 2*(f.quiet+f.support)<f.period;
}
inline bool valid_event(const original::Event& e) {
    if(!e.fhss.enabled)return sparse::valid(e);
    return sparse::environment() && std::isfinite(e.displacement) && e.displacement>=0 &&
        e.displacement<=.25L && supported_fhss(e) && original::valid(sparse::piece_contract_event(e));
}
template<class Mapping>
original::Piece make_runtime_piece(const original::Event& e,original::ProcessingFence fence,
        std::uint64_t at,Mapping& map,std::stop_token stop={}) {
    if(!e.fhss.enabled)return sparse::make_piece(e,fence,at,map,stop);
    if(!valid_event(e))throw Error("Default linear FHSS event unsupported");
    return original::make_piece(sparse::piece_contract_event(e),fence,at,map,stop);
}
inline original::Evaluation evaluate_row(const original::Event& e,const original::Piece& p,std::uint64_t n) {
    if(!e.fhss.enabled)return sparse::evaluate(e,p,n);
    if(!valid_event(e)){original::Evaluation r;r.failure=original::Failure::event_contract;return r;}
    const auto normalized=sparse::piece_contract_event(e);auto r=original::evaluate(normalized,p,n);
    // The shared Sched_Hop polynomial is not clamped. Retain exactly the same
    // tiny proved overshoot recovery as the FHSS-off sparse adapter. FHSS gain
    // is itself clamped; multiplying it does not enlarge this cap.
    if(r.failure==original::Failure::invalid_gain && std::isfinite(r.gain) &&
       r.gain>1 && r.gain<=1+1024*std::numeric_limits<double>::epsilon()) {
        const auto q=r.within/static_cast<long double>(e.chip_samples);
        if(!std::isfinite(q) || q<0 || q>original::owner_count(e)) {
            r.failure=original::Failure::source_coverage;return r;
        }
        const auto owner=static_cast<std::uint64_t>(q);
        if(r.carry)r.carry_owner=owner;else {r.owner=owner;r.current=true;}
        r.failure=original::Failure::none;
    }
    return r;
}
inline constexpr std::uint64_t maximum_fhss_mask_scan=64;
// Cheap sufficient proof of the EXACT gain()==1 branch, not a polynomial or
// libm error certificate. With a positive-rate Original affine piece, physical
// coordinates and RN add/multiply/divide are monotone. Decline 2-second/raw
// wraps; identical executed hop ordinals make distance monotone. Endpoint
// edges >=support imply every intervening row returns exactly 1 before the
// quintic. Rational 2*Fs/5 and lag association are copied from gain(), including
// sample rates not divisible by five. Other cells use bounded exact metadata.
inline bool fhss_flat_span(const original::Event& e,SchedHopCoordinate first,SchedHopCoordinate last) {
    if(!e.fhss.enabled)return true;
    if(!supported_fhss(e) || last.distance(first)<0)return false;
    const auto two_integer=2ULL*e.sample_rate;
    if(first.sample/two_integer!=last.sample/two_integer)return false;
    const auto two=2.L*e.sample_rate;
    const auto position=[&](SchedHopCoordinate x) {
        const auto sample=x.sample%two_integer+x.fraction;
        const auto raw=sample+e.fhss.lag_center;
        const auto hop=std::floor(raw*5/two);
        const auto distance=raw-hop*two/5;
        return std::array<long double,3>{raw,hop,std::min(distance,two/5-distance)-e.fhss.quiet};
    };
    const auto a=position(first),b=position(last);
    for(const auto x:a)if(!std::isfinite(x))return false;
    for(const auto x:b)if(!std::isfinite(x))return false;
    return a[0]>=0 && a[0]<=b[0] && b[0]<two && a[1]==b[1] &&
        a[2]>=e.fhss.support && b[2]>=e.fhss.support;
}
// Immutable after build; all borrows are confined to one synchronous run.
class Context {
    std::array<Lane,maximum_lanes+1> lanes_{};
    std::size_t count_=0;
    unsigned sections_=1;
    std::uint64_t differential_width_=0;
    const Lane* lane(std::size_t global)const {
        for(std::size_t i=0;i<count_;++i)if(lanes_[i].global==global)return &lanes_[i];
        return nullptr;
    }
    static bool same_active(const original::Piece& a,const original::Piece& b) {
        const auto& x=a.location0;const auto& y=b.location0;
        return x.active && y.active && x.frame_index==y.frame_index && x.physical_begin==y.physical_begin &&
            x.physical_end==y.physical_end && x.rise_begin==y.rise_begin && x.flat_begin==y.flat_begin &&
            x.flat_end==y.flat_end && x.fall_end==y.fall_end;
    }
    static long double source_at(const original::Piece& p,long double n) {
        return p.source0+(n-static_cast<long double>(p.anchor_first))*p.rate;
    }
    static SchedHopCoordinate physical_at(const original::Piece& p,long double n) {
        return p.tick0.add_signed((n-static_cast<long double>(p.anchor_first))*p.rate);
    }
    struct Endpoint {const original::Piece* piece=nullptr;long double source=0;double gain=0;};
    static Endpoint endpoint(const Lane& l,long double n) {
        Endpoint out;
        if(!std::isfinite(n) || n<l.event.begin || n>=l.event.end || n<0 || n>=0x1p52L)return out;
        const auto* p=original::find_piece(l.pieces,static_cast<std::uint64_t>(std::floor(n)));
        if(!p || !p->location0.active)return out;
        out.piece=p;out.source=source_at(*p,n);const auto tick=physical_at(*p,n);
        out.gain=SchedHopSourceMap::gain(p->location0,tick);
        if(l.event.fhss.enabled)out.gain*=l.event.fhss.gain(0,
            tick.sample%(2ULL*l.event.sample_rate)+tick.fraction,l.event.sample_rate);
        if(!std::isfinite(out.source) || !std::isfinite(out.gain) || out.gain<0 ||
            out.gain>1+1024*std::numeric_limits<double>::epsilon())return {};
        return out;
    }
public:
    static std::size_t reservation(std::size_t total_pieces,std::size_t lanes) {
        if(!lanes || lanes>maximum_lanes+1 || total_pieces>lanes*maximum_pieces)return std::numeric_limits<std::size_t>::max();
        // The exact frozen-ledger prepass bounds every lane reserve. A second
        // copy allowance covers allocator/vector capacity and metadata scratch.
        return tile::add_size(sizeof(Context)+8192+128*lanes,
            tile::multiply_size(2*total_pieces,sizeof(original::Piece)));
    }
    template<class Mapping,class Fences>
    static std::size_t piece_count(const original::Event& event,Mapping& map,Fences&& fences,std::stop_token stop={}) {
        original::check_stop(stop);
        if(!valid_event(event) || event.begin<event.history_first || event.begin>=event.end)
            throw Error("Default linear Original prepass event unsupported");
        auto cursor=event.begin;std::size_t count=0;
        fences(event.begin,event.end,[&](const auto& raw) {
            original::check_stop(stop);
            if(raw.original_first>cursor || raw.original_count>UINT64_MAX-raw.original_first)
                throw Error("Default linear Original prepass ledger gap");
            const auto finish=std::min(event.end,raw.original_first+raw.original_count);
            if(finish<=cursor)return;
            const original::ProcessingFence fence{raw.original_first+1,event.identity.capture,cursor,finish};
            while(cursor<finish) {
                original::check_stop(stop);
                if(count==maximum_pieces)throw Error("Default linear Original prepass piece cap");
                cursor=make_runtime_piece(event,fence,cursor,map,stop).end;++count;
            }
        });
        if(cursor!=event.end)throw Error("Default linear Original prepass incomplete");
        original::check_stop(stop);return count;
    }
    Context(unsigned sections,std::uint64_t differential_width):sections_(sections),differential_width_(differential_width) {
        if((sections!=1 && sections!=4))throw Error("Default linear sections unsupported");
    }
    template<class Mapping,class Fences>
    void add(std::size_t global,original::Event event,Mapping& map,Fences&& fences,std::stop_token stop={},std::size_t expected_pieces=0) {
        original::check_stop(stop);
        if(count_==maximum_lanes+1 || lane(global) || !valid_event(event) ||
           event.begin<event.history_first || event.begin>=event.end)
            throw Error("Default linear Original event unsupported");
        if(!expected_pieces)expected_pieces=piece_count(event,map,fences,stop);
        if(expected_pieces>maximum_pieces)throw Error("Default linear preflight piece cap");
        auto& out=lanes_[count_];out.pieces.clear();out.global=global;out.event=event;out.pieces.reserve(expected_pieces);
        if(out.pieces.capacity()>2*expected_pieces)throw Error("Default linear vector reserve exceeded preflight");
        auto cursor=event.begin;
        fences(event.begin,event.end,[&](const auto& raw) {
            original::check_stop(stop);
            if(raw.original_first>cursor || raw.original_count>UINT64_MAX-raw.original_first)
                throw Error("Default linear processing ledger gap");
            const auto finish=std::min(event.end,raw.original_first+raw.original_count);
            if(finish<=cursor)return;
            const original::ProcessingFence fence{raw.original_first+1,event.identity.capture,cursor,finish};
            while(cursor<finish) {
                original::check_stop(stop);
                if(out.pieces.size()==expected_pieces)throw Error("Default linear processing prepass changed");
                auto piece=make_runtime_piece(event,fence,cursor,map,stop);
                cursor=piece.end;out.pieces.push_back(std::move(piece));
            }
        });
        if(cursor!=event.end || out.pieces.size()!=expected_pieces)throw Error("Default linear Original processing coverage incomplete");
        original::check_stop(stop);++count_;
    }
    // Actual owned capacities, including any uncommitted lane allocation.
    // A moved frozen reference recipe must charge these rather than maxpieces.
    std::size_t working_bytes()const {
        auto bytes=sizeof(Context);
        for(const auto& value:lanes_)bytes=tile::add_size(bytes,tile::multiply_size(value.pieces.capacity(),sizeof(original::Piece)));
        return bytes;
    }
    std::size_t lane_count()const noexcept{return count_;}
    unsigned sections()const noexcept{return sections_;}
    std::uint64_t differential_width()const noexcept{return differential_width_;}
    const original::Event& event(std::size_t global)const {
        const auto* l=lane(global);if(!l)throw Error("Default linear lane missing");return l->event;
    }
    std::span<const original::Piece> pieces(std::size_t global)const {
        const auto* l=lane(global);if(!l)throw Error("Default linear Piece lane missing");return l->pieces;
    }
    // Prove one exact ownership partition over EVERY actual integer row.
    // Unknown means per-row exact mask/owner, keeping the same coarse chord.
    events::Partition classify(std::size_t global,std::uint64_t first,std::uint64_t end)const {
        events::Partition out;const auto* l=lane(global);
        if(!l || first>=end)return out;
        const auto& e=l->event;
        if(end<=e.begin || first>=e.end){out.valid=true;return out;}
        if(first<e.begin || end>e.end)return out;
        bool have=false;
        for(auto at=first;at<end;) {
            const auto* p=original::find_piece(l->pieces,at);if(!p)return {};
            const auto finish=std::min(end,p->end);
            events::Partition part;
            if(original::outside_current(e,*p,at,finish)){part.valid=true;}
            else {
                const auto tick_a=physical_at(*p,at),tick_b=physical_at(*p,finish-1);
                if(e.fhss.enabled && !fhss_flat_span(e,tick_a,tick_b)) {
                    // Endpoint positivity cannot exclude an intervening FHSS
                    // quiet interval. Inspect ALL public metadata in small
                    // ramp/edge cells; larger ambiguous cells partition rows.
                    if(finish-at>maximum_fhss_mask_scan)return {};
                    bool part_have=false;
                    for(auto n=at;n<finish;++n) {
                        const auto r=evaluate_row(e,*p,n);if(!r.valid())return {};
                        events::Partition one;one.valid=true;one.observed=r.current;
                        if(r.current) {
                            one.owner=r.owner;
                            while(one.section+1<sections_ && r.within>=drift_boundary(one.section+1,e.symbol_samples,sections_))++one.section;
                            one.window=differential_width_?static_cast<std::uint64_t>(r.within/differential_width_):0;
                        }
                        if(!part_have){part=one;part_have=true;}
                        else if(one.observed!=part.observed || (one.observed &&
                            (one.owner!=part.owner || one.section!=part.section || one.window!=part.window)))return {};
                    }
                } else {
                    const auto a=evaluate_row(e,*p,at),b=evaluate_row(e,*p,finish-1);
                    if(!a.valid() || !b.valid() || !a.current || !b.current || a.owner!=b.owner)return {};
                    // The Sched_Hop envelope has one positive useful interval;
                    // FHSS is either disabled or proved exactly flat above.
                    if(a.gain<=0 || b.gain<=0 || a.raw_source<0 || b.raw_source>=e.symbol_samples ||
                       (!p->location0.continuous && (a.physical.sample<p->location0.rise_begin ||
                         b.physical.sample>=p->location0.fall_end)))return {};
                    unsigned sa=0,sb=0;
                    while(sa+1<sections_ && a.within>=drift_boundary(sa+1,e.symbol_samples,sections_))++sa;
                    while(sb+1<sections_ && b.within>=drift_boundary(sb+1,e.symbol_samples,sections_))++sb;
                    const auto wa=differential_width_?static_cast<std::uint64_t>(a.within/differential_width_):0;
                    const auto wb=differential_width_?static_cast<std::uint64_t>(b.within/differential_width_):0;
                    if(sa!=sb || wa!=wb)return {};
                    part.valid=part.observed=true;part.owner=a.owner;part.section=sa;part.window=wa;
                }
            }
            if(!have){out=part;have=true;}
            else if(part.observed!=out.observed || (part.observed &&
                    (part.owner!=out.owner || part.section!=out.section || part.window!=out.window)))return {};
            at=finish;
        }
        out.valid=have;return out;
    }
    template<class Shape>
    events::Row row(std::size_t global,std::uint64_t n,bool need_q,Shape&& shape)const {
        events::Row out;const auto* l=lane(global);if(!l)throw Error("Default linear row lane missing");
        if(n<l->event.begin || n>=l->event.end)return out;
        const auto* p=original::find_piece(l->pieces,n);if(!p)throw Error("Default linear row piece missing");
        const auto r=evaluate_row(l->event,*p,n);
        if(!r.valid())throw Error("Default linear Original row invalid");
        out.source=r.within;out.owner=r.owner;out.observed=r.current;out.gain=r.gain;
        if(out.observed && need_q)out.q=shape(r.within,r.gain);
        return out;
    }
    // Global family chord: source phase never changes with the cohort origin.
    // Product endpoints handle rise/fall ramps without per-row private shaping.
    template<class Coefficients,class Shape>
    events::Cell cell(std::size_t reference_global,long double left,long double right,unsigned subdivisions,
                      double amplitude,Coefficients&& coefficient,Shape&& shape,std::stop_token stop={})const {
        events::Cell out;const auto* l=lane(reference_global);if(!l)return out;
        if(std::isfinite(left) && std::isfinite(right) && left>=0 && right>left && right<0x1p52L) {
            const auto first=static_cast<std::uint64_t>(std::ceil(left));
            const auto end=static_cast<std::uint64_t>(std::ceil(right));
            const auto quiet=classify(reference_global,first,end);
            if(quiet.valid && !quiet.observed) {
                // The chosen global template is zero here. Keep observed=true:
                // each shifted lane STILL supplies its exact Original mask and
                // count. A cohort boundary cannot silently discard gained rows.
                out.chord.supported=out.chord.observed=true;out.chord.residual={0,0};out.outer_zero=true;
                bool all_empty=true;
                for(std::size_t i=0;i<count_;++i) {
                    const auto& candidate=lanes_[i];
                    // step=Q/2, hence this is exactly the candidate's grid
                    // translation. Two outward physical neighbors also cover
                    // separately associated LD endpoint products. Actual
                    // Original membership is proved over the WHOLE superset.
                    const auto shift=(static_cast<long double>(candidate.global)-reference_global)*
                        l->event.chip_samples/(2*l->event.rate);
                    const auto lo=static_cast<std::uint64_t>(std::clamp(std::floor(left+shift)-2,
                        static_cast<long double>(candidate.event.history_first),static_cast<long double>(candidate.event.capture_end)));
                    const auto hi=static_cast<std::uint64_t>(std::clamp(std::ceil(right+shift)+2,
                        static_cast<long double>(candidate.event.history_first),static_cast<long double>(candidate.event.capture_end)));
                    if(lo<hi) {
                        const auto proof=classify(candidate.global,lo,hi);
                        if(!proof.valid || proof.observed){all_empty=false;break;}
                    }
                }
                // Only this all-lane Original no-observation proof permits a
                // numerical skip. Private/public chosen templates stay zero.
                if(all_empty)out.chord.observed=false;
                return out;
            }
        }
        const auto a=endpoint(*l,left),b=endpoint(*l,right);
        if(!a.piece || !b.piece || !same_active(*a.piece,*b.piece) || !(right>left))return out;
        // No calendar/null cycle may hide between the two endpoint pieces.
        auto at=static_cast<std::uint64_t>(std::floor(left));
        const auto finish=static_cast<std::uint64_t>(std::floor(right));
        while(at<=finish) {
            const auto* p=original::find_piece(l->pieces,at);if(!p || !same_active(*a.piece,*p))return {};
            if(p->end>finish)break;at=p->end;
        }
        const auto width=static_cast<long double>(l->event.chip_samples)/subdivisions;
        const auto node=std::round(a.source/width);
        if(node<0 || node>=0x1p40L || a.source<0 || b.source>=l->event.symbol_samples ||
           std::abs(a.source-node*width)>.001L || std::abs(b.source-(node+1)*width)>.001L)return out;
        static_cast<void>(coefficient);original::check_stop(stop);
        if(!std::isfinite(amplitude) || amplitude<=0 || amplitude>0x1p128)return out;
        const auto left_q=shape(static_cast<double>(node*width));
        const auto right_q=shape(static_cast<double>((node+1)*width));
        for(unsigned bit=0;bit<2;++bit) {
            if(!scheduled_acquisition_certificate::finite(left_q[bit]) || !scheduled_acquisition_certificate::finite(right_q[bit]))return {};
            out.chord.left[bit]=a.gain*pattern_limit_pcm(amplitude*left_q[bit])/amplitude;
            out.chord.right[bit]=b.gain*pattern_limit_pcm(amplitude*right_q[bit])/amplitude;
            if(!scheduled_acquisition_certificate::finite(out.chord.left[bit]) ||
               !scheduled_acquisition_certificate::finite(out.chord.right[bit]))return {};
        }
        out.chord.supported=out.chord.observed=true;out.chord.residual={0,0};
        out.source_node=static_cast<std::uint64_t>(node);
        out.outer_gain_left=a.gain;out.outer_gain_right=b.gain;
        return out;
    }
};
struct CacheKey {
    std::uint64_t capture=0,processing_end=0,block_end=0,index=0;
    std::size_t family=0,range_first=0,range_end=0,origin_first=0,outputs=0;
    unsigned subdivisions=0;
    bool operator==(const CacheKey&)const=default;
};
struct Cache {
    CacheKey key;
    events::Geometry geometry;
    std::array<long double,maximum_lanes> observed_source_begin{};
    events::Result result;
    std::size_t reserved_bytes=0;
    void bind(){geometry.observed_source_begin=std::span<const long double>(observed_source_begin).first(key.outputs);}
    std::size_t working_bytes()const{return reserved_bytes;}
};
struct Decision {
    events::Scores scores;
    bool ready=false;
};
} // namespace datapump::modem::detail::scheduled_default_linear_runtime
