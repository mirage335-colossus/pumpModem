#pragma once
#include "scheduled_default_linear_cellstats.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem::detail::scheduled_default_linear_events {
namespace point=scheduled_default_linear_cellstats;
namespace grid=scheduled_linear_grid;
namespace fft=scheduled_linear_grid_fft;
using Complex=std::complex<double>;
using Pair=grid::Pair;
inline constexpr std::uint64_t no_owner=std::numeric_limits<std::uint64_t>::max();
inline constexpr std::size_t maximum_outputs=8;
struct Geometry {
    long double physical_base=0,node_spacing=1;
    std::uint64_t capture_first=0,capture_end=0,symbol_samples=0,chip_samples=0;
    std::size_t template_cells=0,outputs=0,origin_first=0,step=1,fragment_cells=1024;
    unsigned subdivisions=4,sections=1;
    std::uint64_t differential_width=0;
    bool outer=false;
    // Original source coordinates relative to this event's symbol start.
    std::span<const long double> observed_source_begin;
};
struct Cell {
    grid::Cell chord;
    std::uint64_t source_node=no_owner;
    double outer_gain_left=1,outer_gain_right=1;
    bool outer_zero=false; // explicit chosen PUBLIC-basis zero, independent of private coefficients
};
// A caller proof about EVERY captured physical integer in the requested cell.
// An invalid classification triggers a row partition, not a missing cell.
struct Partition {
    bool valid=false,observed=false;
    std::uint64_t owner=no_owner,window=0;
    unsigned section=0;
};
struct Row {
    Pair q{};
    long double source=0;
    std::uint64_t owner=no_owner;
    bool observed=false;
    double gain=1; // actual independent-public-pulse gain on Original fallback rows
};
struct Work {
    std::uint64_t template_cells=0,supported_cells=0,quiet_cells=0,unsupported_cells=0,
        shared_input_rows=0,cell_contractions=0,partitioned_cells=0,direct_input_rows=0,
        direct_selected_rows=0,projection_lookups=0,source_rows=0,outer_terms=0;
    std::size_t peak_bytes=0;
};
struct EventStats {
    std::array<CorrelationFit,2> whole{};
    std::array<CorrelationChipEvidence,2> chips{};
    std::array<CorrelationDriftFit,2> drift{};
    std::array<CorrelationDifferentialFit,2> differential{};
    std::array<unsigned char,2> quarter_ranks{};
    CorrelationOuterEvidence outer;
    std::uint64_t observed_start=no_owner;
    std::uint64_t last_owner=no_owner,last_window=0;
    unsigned last_section=0;
    bool valid=false;
    std::size_t working_bytes()const{return sizeof(*this);}
};
struct Result {
    std::vector<EventStats> events;
    Work work;
    bool valid=false;
    std::size_t working_bytes()const{return sizeof(*this)+events.capacity()*sizeof(EventStats);}
};
struct Scores {
    std::array<double,2> score{};
    unsigned bit=0;
    double outer_presence_score=0;
    bool outer_presence_active=false,valid=false;
};
// The outer test is an additional admission AND gate, never a promotion.
// Caller still owns cold-lane eligibility and the charged retention/trial floor.
inline bool below_rejection_floor(const Scores& value,double floor) {
    return value.valid && std::isfinite(floor) && floor>=0 &&
        std::isfinite(value.score[0]) && std::isfinite(value.score[1]) &&
        value.score[0]>=0 && value.score[1]>=0 &&
        std::max(value.score[0],value.score[1])<floor;
}
inline bool finite_fit(const CorrelationFit& f) {
    return std::isfinite(f.xc)&&std::isfinite(f.xs)&&std::isfinite(f.cc)&&std::isfinite(f.ss)&&
        std::isfinite(f.cs)&&std::isfinite(f.energy)&&f.cc>=0&&f.ss>=0&&f.energy>=0;
}
inline bool full_rank(const CorrelationFit& f) {
    return f.count>2&&f.cc*f.ss-f.cs*f.cs>1e-12*std::max(1.,f.cc*f.ss);
}
inline void join(CorrelationFit& a,const CorrelationFit& b) {
    fft::require(b.count<=no_owner-a.count,"Default linear fit count overflow");
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
    fft::require(finite_fit(a),"Default linear joined fit is invalid");
}
inline bool valid_geometry(const Geometry& g) {
    return g.outputs&&g.outputs<=maximum_outputs&&g.template_cells&&g.template_cells<=0x40000000ULL&&g.fragment_cells&&g.fragment_cells<=8192&&g.step&&
        g.origin_first<=0x40000000ULL/g.step&&g.outputs-1<=0x40000000ULL/g.step-g.origin_first&&
        std::isfinite(g.physical_base)&&std::isfinite(g.node_spacing)&&g.node_spacing>0&&g.node_spacing<=0x1p32L&&
        g.capture_first<=g.capture_end&&g.capture_end<=0x10000000000ULL&&g.symbol_samples&&g.chip_samples&&
        g.symbol_samples%g.chip_samples==0&&(g.subdivisions==2||g.subdivisions==4||g.subdivisions==8||g.subdivisions==16)&&
        (g.sections==1||g.sections==4)&&(!g.differential_width||g.differential_width<=g.symbol_samples)&&
        (g.observed_source_begin.empty()||g.observed_source_begin.size()==g.outputs);
}
inline std::size_t retained_reservation(std::size_t outputs) {
    fft::require(outputs&&outputs<=maximum_outputs,"Default linear retained output bound");
    return fft::add(sizeof(Result)+4096,fft::mul(outputs,sizeof(EventStats)));
}
inline std::size_t projection_capacity(const Geometry& g) {
    const auto extent=fft::add(std::min(g.fragment_cells,g.template_cells),fft::mul(g.outputs-1,g.step));
    const auto rows=std::ceil(static_cast<long double>(extent)*g.node_spacing)+8;
    fft::require(std::isfinite(rows)&&rows>0&&rows<=1048576,"Default linear projection tile unsupported");
    return static_cast<std::size_t>(rows);
}
inline std::size_t reservation(const Geometry& g) {
    fft::require(valid_geometry(g),"Default linear invalid geometry");
    const auto extent=fft::add(std::min(g.fragment_cells,g.template_cells),fft::mul(g.outputs-1,g.step));
    return fft::add(retained_reservation(g.outputs),fft::add(8192,
        fft::add(fft::add(point::Moments::reservation(extent),fft::mul(std::min(g.fragment_cells,g.template_cells),sizeof(Cell))),
            fft::mul(projection_capacity(g),sizeof(CorrelationProjection)))));
}
inline unsigned section_at(const Geometry& g,long double source) {
    unsigned section=0;
    while(section+1<g.sections&&source>=drift_boundary(section+1,g.symbol_samples,g.sections))++section;
    return section;
}
inline std::uint64_t window_at(const Geometry& g,long double source) {
    return g.differential_width?static_cast<std::uint64_t>(std::min(std::floor(std::max(0.L,source)/g.differential_width),
        static_cast<long double>(g.symbol_samples/g.differential_width))):0;
}
inline long double observed_begin(const Geometry& g,std::size_t j) {
    return g.observed_source_begin.empty()?0:g.observed_source_begin[j];
}
inline void advance(EventStats& out,const Geometry& g,std::size_t j,const Partition& p) {
    fft::require(p.owner<g.symbol_samples/g.chip_samples&&p.section<g.sections&&
        (!g.differential_width||p.window<=g.symbol_samples/g.differential_width),"Default linear partition outside source symbol");
    fft::require(out.last_owner==no_owner||(p.owner>=out.last_owner&&p.section>=out.last_section&&p.window>=out.last_window),
        "Default linear source partition rewound");
    for(unsigned bit=0;bit<2;++bit) {
        auto& d=out.drift[bit];
        while(d.section<p.section) {
            if(full_rank(d.active))out.quarter_ranks[bit]|=static_cast<unsigned char>(1U<<d.section);
            const auto e=d.active.explained();d.explained_sum+=e;d.largest_explained=std::max(d.largest_explained,e);
            d.active={};++d.section;
        }
        if(g.differential_width) {
            auto& w=out.differential[bit];
            if(!w.initialized||w.window_index!=p.window) {
                if(w.initialized) {
                    w.expected_count=static_cast<long double>(w.window_index)*g.differential_width>=observed_begin(g,j)?w.active.count:0;
                    w.finish(g.symbol_samples,g.differential_width);
                }
                w.active={};w.window_index=p.window;w.initialized=true;w.expected_count=0;
            }
        }
    }
    out.last_owner=p.owner;out.last_section=p.section;out.last_window=p.window;
}
inline void add(EventStats& out,const Geometry& g,std::size_t j,const Partition& p,
        const std::array<CorrelationFit,2>& fit,std::uint64_t first) {
    fft::require(p.valid&&p.observed&&fit[0].count==fit[1].count,"Default linear incomplete partition");
    if(!fit[0].count)return;
    advance(out,g,j,p);if(out.observed_start==no_owner)out.observed_start=first;
    for(unsigned bit=0;bit<2;++bit) {
        fft::require(finite_fit(fit[bit]),"Default linear invalid contribution");
        join(out.whole[bit],fit[bit]);out.chips[bit].add_fit(fit[bit],p.owner);
        if(g.sections>1)join(out.drift[bit].active,fit[bit]);
        if(g.differential_width)join(out.differential[bit].active,fit[bit]);
    }
}
// Same scheduled score/guard branches as complete(); no Original error bound or
// admission decision is implied. The caller supplies the charged threshold.
inline Scores scores(const EventStats& out,const Geometry& g,double threshold) {
    Scores result;if(!out.valid||!valid_geometry(g)||!std::isfinite(threshold)||threshold<0)return result;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& whole=out.whole[bit];const auto& chips=out.chips[bit];
        auto score=whole.score();if(chips.colored_excess(whole,threshold))score=std::min(score,chips.score(whole));
        if(g.sections>1) {
            const auto& d=out.drift[bit];const auto all=(1U<<(g.sections-1))-1;
            bool aligned=true;for(unsigned k=1;k<g.sections;++k)aligned&=drift_boundary(k,g.symbol_samples,g.sections)%g.chip_samples==0;
            double section_score;
            if(!aligned||d.section+1!=g.sections||out.quarter_ranks[bit]!=all||!full_rank(d.active))
                section_score=d.score(whole,g.sections,g.chip_samples);
            else {
                const auto last=d.active.explained(),full=d.explained_sum+last;
                const auto retained=full-std::max(d.largest_explained,last);
                section_score=drift_evidence(retained,whole.energy,static_cast<double>(whole.count),g.sections,true);
                const auto [last_energy,last_rank]=CorrelationChipEvidence::projection(chips.cell);
                const auto rank=chips.projected_rank+last_rank;
                const auto projected=std::clamp(chips.projected_energy+last_energy,0.,whole.energy);
                const auto fitted_rank=2U*g.sections;
                if(chips.nested&&(CorrelationChipEvidence::zero_basis(chips.cell)||last_rank>=std::min<std::uint64_t>(2,chips.cell.count))&&
                    rank>fitted_rank&&rank<whole.count) {
                    const auto rr=static_cast<double>(rank-fitted_rank),dd=static_cast<double>(whole.count-rank);
                    constexpr double tail=30;const auto delta=4*threshold+32;
                    const auto lower=dd-2*std::sqrt(dd*tail);
                    if(lower>0) {
                        const auto upper=rr+delta+2*std::sqrt((rr+2*delta)*tail)+2*tail;
                        const auto residual=whole.energy-projected,excess=std::max(0.,projected-full);
                        if(residual<=0||excess*dd>std::max(2.,(upper/rr)/(lower/dd))*residual*rr)
                            section_score=std::min(section_score,drift_evidence(retained,projected,static_cast<double>(rank),g.sections,true));
                    }
                }
            }
            score=combine_drift_evidence(score,section_score,g.sections);
        }
        if(g.differential_width)score=combine_differential_evidence(score,
            out.differential[bit].score(g.symbol_samples,g.differential_width),true);
        if(!std::isfinite(score)||score<0)return result;result.score[bit]=score;
    }
    result.bit=result.score[1]>result.score[0]?1U:0U;
    if(g.outer) {
        result.outer_presence_score=out.outer.score(result.bit);
        result.outer_presence_active=out.outer.can_admit(result.bit,threshold)&&
            out.chips[result.bit].colored_excess(out.whole[result.bit],threshold);
        if(!std::isfinite(result.outer_presence_score))return result;
    }
    result.valid=true;return result;
}

// Cell/Visit/Classify/Original/Coefficients obey the documented same-global-grid
// contract. All arguments j passed to callbacks are ORIGINAL global lane j.
// No lane has an Original certified rejection merely because this returns valid.
template<class CellFn,class Visit,class Classify,class Original,class Coefficients>
Result run(const Geometry& g,std::size_t budget,CellFn&& make_cell,Visit&& visit,Classify&& classify,
        Original&& original,Coefficients&& coefficients,std::stop_token stop={}) {
    fft::require(valid_geometry(g),"Default linear invalid event geometry");
    const auto needed=reservation(g);fft::require(needed<=budget,"Default linear event workspace preflight");
    const auto first_origin=fft::mul(g.origin_first,g.step),halo=fft::mul(g.outputs-1,g.step);
    Result out;out.events.resize(g.outputs);std::vector<Cell> cells;cells.reserve(std::min(g.fragment_cells,g.template_cells));
    std::vector<CorrelationProjection> projected;projected.reserve(projection_capacity(g));
    fft::require(projected.capacity()<=projection_capacity(g),"Default linear projection capacity exceeded charged bound");
    struct CleanCells {std::vector<Cell>& cells;~CleanCells(){if(cells.data())OPENSSL_cleanse(cells.data(),cells.capacity()*sizeof(Cell));}} clean_cells{cells};
    struct CleanProjection {std::vector<CorrelationProjection>& rows;~CleanProjection(){if(rows.data())OPENSSL_cleanse(rows.data(),rows.capacity()*sizeof(CorrelationProjection));}} clean_projection{projected};
    const auto physical=[&](std::size_t node){const auto x=g.physical_base+static_cast<long double>(node)*g.node_spacing;
        fft::require(std::isfinite(x)&&x>=-0x1p40L&&x<=0x1p41L,"Default linear physical extent");return x;};
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;};
    const auto bounds=[&](std::size_t a,std::size_t b){return std::pair{
        static_cast<std::uint64_t>(std::clamp(std::floor(physical(a))-2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end))),
        static_cast<std::uint64_t>(std::clamp(std::ceil(physical(b))+2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)))};};
    std::array<std::array<double,17>,16> public_left{},public_right{};
    if(g.outer)for(unsigned phase=0;phase<g.subdivisions;++phase)for(int relative=-8;relative<=8;++relative) {
        public_left[phase][relative+8]=pattern_pulse(static_cast<double>(phase)/g.subdivisions-relative-.5);
        public_right[phase][relative+8]=pattern_pulse(static_cast<double>(phase+1)/g.subdivisions-relative-.5);
    }
    const auto pulse=[&](const Cell& c,auto&& consume) {
        if(c.outer_zero)return;
        fft::require(c.source_node!=no_owner&&c.source_node/g.subdivisions<g.symbol_samples/g.chip_samples&&
            std::isfinite(c.outer_gain_left)&&std::isfinite(c.outer_gain_right)&&c.outer_gain_left>=0&&c.outer_gain_right>=0&&
            c.outer_gain_left<=scheduled_cold_colored::smooth_gain_upper()&&c.outer_gain_right<=scheduled_cold_colored::smooth_gain_upper(),
            "Default linear outer source identity unavailable");
        const auto owner=c.source_node/g.subdivisions;const auto phase=c.source_node%g.subdivisions;
        for(int relative=-8;relative<=8;++relative) {
            if(relative<0&&owner<static_cast<std::uint64_t>(-relative))continue;
            const auto local=relative<0?owner-static_cast<std::uint64_t>(-relative):owner+relative;
            if(local>=g.symbol_samples/g.chip_samples)continue;
            consume(local,c.outer_gain_left*public_left[phase][relative+8],c.outer_gain_right*public_right[phase][relative+8]);
        }
    };
    for(std::size_t offset=0;offset<g.template_cells;) {
        fft::stop(stop);const auto actual=std::min(g.fragment_cells,g.template_cells-offset);cells.clear();bool useful=false;
        std::size_t first_needed=actual,last_needed=0;
        for(std::size_t i=0;i<actual;++i) {
            if((i&63U)==0)fft::stop(stop);const auto k=offset+i;
            auto c=make_cell(k,physical(k),physical(k+1));++out.work.template_cells;
            if(c.chord.supported) {
                if(c.chord.observed){useful=true;++out.work.supported_cells;}
                else ++out.work.quiet_cells;
            }else ++out.work.unsupported_cells;
            if(!c.chord.supported||c.chord.observed){first_needed=std::min(first_needed,i);last_needed=i+1;}
            cells.push_back(std::move(c));
        }
        const auto input_first=fft::add(offset,first_origin),extent=fft::add(actual,halo);
        std::optional<point::Moments> moments;
        std::uint64_t projected_first=0,projected_end=0;
        if(first_needed<last_needed) {
            if(useful)moments.emplace(extent,point::Moments::reservation(extent));
            const auto [a,b]=bounds(fft::add(input_first,first_needed),fft::add(input_first,fft::add(last_needed,halo)));
            fft::require(b-a<=projection_capacity(g),"Default linear actual projection tile exceeded bound");
            projected.resize(static_cast<std::size_t>(b-a));projected_first=a;projected_end=b;auto next=a;
            if(a<b)visit(a,b,[&](std::uint64_t n,const CorrelationProjection& p){
                if((out.work.shared_input_rows++&255U)==0)fft::stop(stop);
                fft::require(n==next&&n<b,"Default linear physical projection tile missing/repeated row");
                projected[static_cast<std::size_t>(n-a)]=p;++next;
                const auto u=node_of(n)-input_first;if(moments&&u>=0&&u<extent)moments->add(n,u,p,1,true);
            });fft::require(next==b,"Default linear physical projection tile incomplete");
            if(moments)moments->finish();
        }
        const auto live=fft::add(out.working_bytes()+4096,fft::add(projected.capacity()*sizeof(CorrelationProjection),
            fft::add(cells.capacity()*sizeof(Cell),moments?moments->working_bytes():0)));
        out.work.peak_bytes=std::max(out.work.peak_bytes,live);fft::require(live<=needed,"Default linear live workspace exceeds reservation");
        for(std::size_t j=0;j<g.outputs;++j) {
            fft::stop(stop);auto& target=out.events[j];const auto global=fft::add(g.origin_first,j),shift=fft::mul(global,g.step);
            for(std::size_t i=0;i<actual;++i) {
                if((i&63U)==0)fft::stop(stop);const auto& c=cells[i];if(c.chord.supported&&!c.chord.observed)continue;
                const auto at=fft::add(offset+i,shift);const auto [a,b]=bounds(at,at+1);
                // Invert the SAME executed node_of expression used by moments.
                // A ceil of the separately multiplied physical endpoint can
                // round to the opposite side of a sampled boundary.
                const auto lower=[&](long double threshold){auto lo=a,hi=b;while(lo<hi){const auto mid=lo+(hi-lo)/2;
                    if(node_of(mid)<threshold)lo=mid+1;else hi=mid;}return lo;};
                const auto first=lower(at),end=lower(static_cast<long double>(at)+1);
                const auto partition=c.chord.supported?classify(global,first,end):Partition{};
                if(c.chord.supported&&partition.valid) {
                    if(!partition.observed)continue;
                    fft::require(bool(moments),"Default linear missing shared moments");const auto local=i+j*g.step;
                    const auto n=moments->count()[local];if(!n&&first==end)continue;
                    const auto fit=n==end-first?point::contract_centers(moments->rows()[local],c.chord,static_cast<double>(moments->energy()[local]),n):point::CellStats{};
                    if(fit.valid) {
                        add(target,g,j,partition,fit.fits,first);++out.work.cell_contractions;
                        if(g.outer)pulse(c,[&](std::uint64_t chip,double l,double r){
                            target.outer.add(chip,l*moments->rows()[local][0]+r*moments->rows()[local][1],coefficients);++out.work.outer_terms;
                        });continue;
                    }
                }
                ++out.work.partitioned_cells;
                // Reuse the same captured projection tile. Owner/window cuts
                // never trigger another raw-fence copy or carrier prefix run.
                fft::require(first>=projected_first&&end<=projected_end,"Default linear row partition outside shared projection tile");
                for(auto n=first;n<end;++n){
                    if((out.work.projection_lookups++&255U)==0)fft::stop(stop);const auto u=node_of(n);
                    const auto& p=projected[static_cast<std::size_t>(n-projected_first)];
                    auto row=original(global,n,!c.chord.supported);if(!row.observed)continue;
                    fft::require(std::isfinite(row.source)&&row.source>=0&&row.source<g.symbol_samples&&
                        row.owner==static_cast<std::uint64_t>(std::floor(row.source/g.chip_samples)),"Default linear original row owner mismatch");
                    Pair q=row.q;const auto t=static_cast<double>(u-at);
                    if(c.chord.supported)for(unsigned bit=0;bit<2;++bit)q[bit]=(1-t)*c.chord.left[bit]+t*c.chord.right[bit];
                    else ++out.work.source_rows;
                    std::array<CorrelationFit,2> fits;
                    for(unsigned bit=0;bit<2;++bit){fft::require(scheduled_acquisition_certificate::finite(q[bit]),"Default linear nonfinite row template");fits[bit].add(p,q[bit],1);}
                    const Partition part{true,true,row.owner,window_at(g,row.source),section_at(g,row.source)};
                    add(target,g,j,part,fits,n);++out.work.direct_selected_rows;
                    if(g.outer) {
                        const Complex measured{p.xc,p.xs};
                        if(c.chord.supported)pulse(c,[&](std::uint64_t chip,double l,double r){target.outer.add(chip,((1-t)*l+t*r)*measured,coefficients);++out.work.outer_terms;});
                        else pattern_pulse_each(static_cast<double>(row.source),g.symbol_samples,g.chip_samples,[&](std::uint64_t chip,double weight){
                            fft::require(std::isfinite(row.gain)&&row.gain>=0&&row.gain<=scheduled_cold_colored::smooth_gain_upper(),"Default linear Original outer gain invalid");
                            target.outer.add(chip,(row.gain*weight)*measured,coefficients);++out.work.outer_terms;
                        });
                    }
                }
            }
        }
        offset+=actual;
    }
    for(std::size_t j=0;j<g.outputs;++j) {
        fft::stop(stop);auto& event=out.events[j];
        if(g.differential_width)for(auto& d:event.differential)if(d.initialized)
            d.expected_count=static_cast<long double>(d.window_index)*g.differential_width>=observed_begin(g,j)?d.active.count:0;
        if(g.outer){event.outer.finish(coefficients);for(unsigned bit=0;bit<2;++bit)
            fft::require(std::isfinite(event.outer.dot[bit].real())&&std::isfinite(event.outer.dot[bit].imag())&&
                std::isfinite(event.outer.variance[bit])&&event.outer.variance[bit]>=0,"Default linear completed outer fields invalid");}
        event.valid=event.whole[0].count&&event.whole[0].count==event.whole[1].count&&finite_fit(event.whole[0])&&finite_fit(event.whole[1]);
    }
    out.valid=std::all_of(out.events.begin(),out.events.end(),[](const auto& e){return e.valid;});fft::stop(stop);return out;
}
} // namespace datapump::modem::detail::scheduled_default_linear_events
