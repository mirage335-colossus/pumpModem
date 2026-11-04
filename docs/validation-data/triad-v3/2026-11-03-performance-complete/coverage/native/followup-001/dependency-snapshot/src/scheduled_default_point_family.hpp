#pragma once
// Experimental Default POINT operator. This computes the SAME chosen global
// endpoint chords as the full point guard, with exact Original selected rows.
// It is neither an Original-template certificate nor a numerical score bound.
#include "scheduled_default_point_fft.hpp"
#include "scheduled_default_point_observation.hpp"
#include "scheduled_default_linear_cellstats.hpp"
#include "scheduled_default_linear_events.hpp"
#include "scheduled_default_point_guard_archive.hpp"
#include "scheduled_acquisition_tile.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace datapump::modem::detail::scheduled_default_point_family {
namespace fft=scheduled_default_point_fft;
namespace grid=scheduled_linear_grid;
namespace point=scheduled_default_linear_cellstats;
namespace events=scheduled_default_linear_events;
namespace observation=scheduled_default_point_observation;
namespace tile=scheduled_acquisition_tile;
using Pair=grid::Pair;
struct Geometry {
    long double physical_base=0,node_spacing=1;
    std::uint64_t capture_first=0,capture_end=0;
    std::size_t template_cells=0,outputs=0,origin_first=0,step=1,fragment_cells=1024;
};
struct Limits {
    // Exceeding a work cap aborts the OPTIONAL family; caller takes its normal
    // full point/Original fallback. No lane or source row is omitted.
    std::uint64_t maximum_mask_rows=16777216,maximum_original_rows=16777216;
};
struct OriginalRow {Pair q{};bool observed=false;};
struct Work {
    fft::Work transforms;
    std::uint64_t template_cells=0,supported_cells=0,zero_cells=0,unsupported_cells=0,
        shared_input_rows=0,selected_rows=0,mask_rows=0,original_rows=0,
        mask_runs=0,original_runs=0,skipped_fragments=0;
    std::size_t peak_bytes=0;
};
struct Result {
    std::vector<std::array<CorrelationFit,2>> whole;
    std::vector<std::uint8_t> valid;
    Work work;
    std::size_t working_bytes()const {
        return sizeof(*this)+whole.capacity()*sizeof(whole[0])+valid.capacity()*sizeof(valid[0]);
    }
};
inline bool valid_geometry(const Geometry& g) {
    return g.outputs&&g.outputs<=tile::maximum_start_span&&g.template_cells&&g.template_cells<=0x40000000ULL&&
        g.fragment_cells&&g.fragment_cells<=8192&&g.step&&g.step<=64&&
        g.origin_first<=0x40000000ULL/g.step&&g.outputs-1<=0x40000000ULL/g.step-g.origin_first&&
        std::isfinite(g.physical_base)&&std::isfinite(g.node_spacing)&&g.node_spacing>0&&g.node_spacing<=0x1p32L&&
        g.capture_first<=g.capture_end&&g.capture_end<=0x10000000000ULL;
}
inline std::size_t projection_capacity(const Geometry& g) {
    const auto extent=tile::add_size(std::min(g.fragment_cells,g.template_cells),tile::multiply_size(g.outputs-1,g.step));
    const auto rows=std::ceil(static_cast<long double>(extent)*g.node_spacing)+8;
    if(!std::isfinite(rows)||rows<=0||rows>1048576)throw Error("point FFT projection tile unsupported");
    return static_cast<std::size_t>(rows);
}
inline std::size_t retained_reservation(std::size_t outputs) {
    if(!outputs||outputs>tile::maximum_start_span)throw Error("point FFT retained extent invalid");
    return tile::add_size(sizeof(Result)+4096,tile::multiply_size(outputs,sizeof(std::array<CorrelationFit,2>)+sizeof(std::uint8_t)));
}
template<class T>std::size_t reservation(const Geometry& g) {
    if(!valid_geometry(g))throw Error("point FFT invalid family geometry");
    const auto fragment=std::min(g.fragment_cells,g.template_cells);
    const auto extent=tile::add_size(fragment,tile::multiply_size(g.outputs-1,g.step));
    const fft::Geometry fg{fragment,g.outputs,g.step,fragment,true,false};
    auto n=tile::add_size(retained_reservation(g.outputs),fft::StreamingEngine<T>::reservation(fg));
    n=tile::add_size(n,point::Moments::reservation(extent));
    n=tile::add_size(n,tile::multiply_size(projection_capacity(g),sizeof(CorrelationProjection)+sizeof(long double)));
    n=tile::add_size(n,tile::multiply_size(fragment,sizeof(events::Cell)+sizeof(fft::TemplateRow)+3*sizeof(std::size_t)));
    n=tile::add_size(n,tile::multiply_size(g.outputs,2*sizeof(std::array<CorrelationFit,2>)+sizeof(long double)+sizeof(std::uint64_t)));
    return tile::add_size(n,16384);
}
inline bool nonzero(const grid::Cell& c) {
    return c.left[0]!=Pair::value_type{}||c.left[1]!=Pair::value_type{}||
        c.right[0]!=Pair::value_type{}||c.right[1]!=Pair::value_type{};
}
// This validity gate is a numerical FALLBACK, not a certificate. In particular,
// an FFT singular/negative Gram must not become a spurious zero-score reject.
inline bool usable(const CorrelationFit& f) {
    if(!events::finite_fit(f)||f.count<=2||f.energy<=1e-30||f.cc<=0||f.ss<=0)return false;
    const auto det=f.cc*f.ss-f.cs*f.cs;
    if(!std::isfinite(det)||det<=1e-12*std::max(1.,f.cc*f.ss))return false;
    // score() intentionally clamps the explained fraction. Do not let an
    // overflowing quadratic reach that clamp and masquerade as finite data.
    const auto a=f.ss*f.xc,b=f.cc*f.xs,c=2*f.cs;
    const auto aa=a*f.xc,bb=b*f.xs,cx=c*f.xc,cross=cx*f.xs;
    const auto sum=aa+bb,numerator=sum-cross,projected=numerator/det,fraction=projected/f.energy;
    for(const auto x:{a,b,c,aa,bb,cx,cross,sum,numerator,projected,fraction})if(!std::isfinite(x))return false;
    const auto score=f.score();return std::isfinite(score)&&score>=0;
}
// Cells use the immutable global template reference (no cohort reset). Every
// callback lane j is GLOBAL origin_first+j. Selection must describe ALL actual
// current positive-gain rows, including zero-q noise observations. Visit returns
// EACH physical integer exactly once using the raw-capture carrier association.
// Original is called ONLY on selected rows of unsupported cells. It MUST use
// the same actual Original piece/q rule as the full point fallback.
template<class T,class Cell,class Visit,class Original>
Result run(const Geometry& g,std::size_t budget,Cell&& make_cell,Visit&& visit,
        std::span<const observation::Selection> selections,Original&& original,
        Limits limits={},std::stop_token stop={},scheduled_default_point_guard_archive::Archive* archive=nullptr,scheduled_default_point_template_cache::Pool<T>* template_pool=nullptr) {
    if(!valid_geometry(g)||selections.size()!=g.outputs)throw Error("point FFT family contract invalid");
    const auto needed=reservation<T>(g);if(needed>budget)throw Error("point FFT family workspace preflight");
    const auto fragment=std::min(g.fragment_cells,g.template_cells),halo=tile::multiply_size(g.outputs-1,g.step);
    const auto first_origin=tile::multiply_size(g.origin_first,g.step);
    if(archive) {
        const auto& a=archive->layout();
        if(a.physical_base!=g.physical_base||a.node_spacing!=g.node_spacing||a.capture_first!=g.capture_first||
           a.capture_end!=g.capture_end||a.template_cells!=g.template_cells||a.input_first!=first_origin||
           a.input_cells!=tile::add_size(g.template_cells,halo)||a.origin_first!=g.origin_first||
           a.outputs!=g.outputs||a.step!=g.step)throw Error("point FFT archive layout mismatch");
    }
    const fft::Geometry fg{fragment,g.outputs,g.step,fragment,true,false};
    // Source-only spectra are charged separately by their optional owner.
    fft::StreamingEngine<T> engine(fg,fft::StreamingEngine<T>::reservation(fg),stop,template_pool);
    Result out;out.whole.resize(g.outputs);out.valid.resize(g.outputs);
    std::vector<std::array<CorrelationFit,2>> correction(g.outputs);
    std::vector<long double> energies(g.outputs);std::vector<std::uint64_t> counts(g.outputs);
    std::vector<events::Cell> cells;cells.reserve(fragment);
    std::vector<fft::TemplateRow> rows;rows.reserve(fragment);
    struct Run {std::size_t first=0,end=0;bool unsupported=false;};
    std::vector<Run> runs;runs.reserve(fragment);
    std::vector<CorrelationProjection> projected;projected.reserve(projection_capacity(g));
    std::vector<long double> prefix;prefix.reserve(projection_capacity(g)+1);
    struct Clean {
        std::vector<events::Cell>&c;std::vector<fft::TemplateRow>&r;std::vector<CorrelationProjection>&p;
        std::vector<std::array<CorrelationFit,2>>&f;
        ~Clean(){if(c.data())OPENSSL_cleanse(c.data(),c.capacity()*sizeof(c[0]));
            if(r.data())OPENSSL_cleanse(r.data(),r.capacity()*sizeof(r[0]));
            if(p.data())OPENSSL_cleanse(p.data(),p.capacity()*sizeof(p[0]));
            if(f.data())OPENSSL_cleanse(f.data(),f.capacity()*sizeof(f[0]));}
    } clean{cells,rows,projected,correction};
    const auto check_stop=[&]{if(stop.stop_requested())throw Error("point FFT family cancelled");};
    for(const auto& selection:selections) {
        check_stop();if(!selection.valid)throw Error("point FFT selection incomplete");
        std::uint64_t count=0,last=0;
        for(const auto& r:selection.runs) {
            if(r.first>=r.end||r.first<g.capture_first||r.end>g.capture_end||r.first<last||r.end-r.first>UINT64_MAX-count)
                throw Error("point FFT selection extent invalid");
            count+=r.end-r.first;last=r.end;
        }
        if(count!=selection.count)throw Error("point FFT selection count mismatch");
    }
    const auto physical=[&](std::size_t node){const auto p=g.physical_base+static_cast<long double>(node)*g.node_spacing;
        if(!std::isfinite(p)||p< -0x1p40L||p>0x1p41L)throw Error("point FFT physical extent invalid");return p;};
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;};
    const auto bounds=[&](std::size_t first,std::size_t end){return std::pair{
        static_cast<std::uint64_t>(std::clamp(std::floor(physical(first))-2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end))),
        static_cast<std::uint64_t>(std::clamp(std::ceil(physical(end))+2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)))};};
    const auto lower=[&](std::size_t node,std::uint64_t a,std::uint64_t b){while(a<b){const auto m=a+(b-a)/2;
        if(node_of(m)<static_cast<long double>(node))a=m+1;else b=m;}return a;};
    const auto intersections=[&](const observation::Selection& selection,std::uint64_t a,std::uint64_t b,auto&& emit){
        auto i=std::lower_bound(selection.runs.begin(),selection.runs.end(),a,[](const auto&r,auto n){return r.end<=n;});
        for(;i!=selection.runs.end()&&i->first<b;++i){const auto first=std::max(a,i->first),end=std::min(b,i->end);if(first<end)emit(first,end);}
    };
    for(std::size_t offset=0;offset<g.template_cells;) {
        check_stop();const auto actual=std::min(fragment,g.template_cells-offset);cells.clear();rows.clear();runs.clear();
        bool useful=false;
        for(std::size_t i=0;i<actual;++i) {
            if((i&63U)==0)check_stop();auto c=make_cell(offset+i,physical(offset+i),physical(offset+i+1));
            ++out.work.template_cells;
            if(c.chord.supported) {
                // Original selections, not reference/cohort quiet permission,
                // own N/E. A zero reference chord remains exactly zero.
                if(!c.chord.observed){c.chord.left={};c.chord.right={};c.chord.observed=true;}
                for(const auto q:{c.chord.left[0],c.chord.left[1],c.chord.right[0],c.chord.right[1]})
                    if(!std::isfinite(q.real())||!std::isfinite(q.imag()))throw Error("point FFT nonfinite chord");
                ++out.work.supported_cells;
                if(nonzero(c.chord))useful=true;else ++out.work.zero_cells;
            }else ++out.work.unsupported_cells;
            const bool active=!c.chord.supported||nonzero(c.chord);
            if(active) {
                const bool unsupported=!c.chord.supported;
                if(!runs.empty()&&runs.back().end==i&&runs.back().unsupported==unsupported)runs.back().end=i+1;
                else runs.push_back({i,i+1,unsupported});
            }
            rows.push_back(grid::row(c.chord));cells.push_back(std::move(c));
        }
        if(archive)archive->install_cells(offset,cells,stop);
        const auto input_first=tile::add_size(offset,first_origin),extent=tile::add_size(actual,halo);
        const auto [a,b]=bounds(input_first,tile::add_size(input_first,extent));
        bool selected_fragment=false;
        for(std::size_t j=0;j<g.outputs&&!selected_fragment;++j) {
            const auto shift=tile::multiply_size(g.origin_first+j,g.step);
            const auto first=lower(offset+shift,a,b),end=lower(offset+actual+shift,a,b);
            intersections(selections[j],first,end,[&](auto,auto){selected_fragment=true;});
        }
        // This is a selected-support proof across EVERY output, independent
        // of private coefficients. No selected PCM/noise row is skipped.
        if(!selected_fragment){++out.work.skipped_fragments;offset+=actual;continue;}
        const auto capacity=projection_capacity(g);if(b-a>capacity)throw Error("point FFT physical tile exceeds preflight");
        projected.resize(static_cast<std::size_t>(b-a));prefix.resize(projected.size()+1);prefix[0]=0;
        point::Moments moments(extent,point::Moments::reservation(extent));
        std::optional<scheduled_default_point_guard_archive::EdgeCapture> edges;
        if(archive&&archive->edge_width())edges.emplace(*archive,stop);
        auto next=a;
        if(a<b)visit(a,b,[&](std::uint64_t n,const CorrelationProjection&p){
            if((out.work.shared_input_rows++&255U)==0)check_stop();
            if(n!=next||n>=b||!std::isfinite(p.energy)||p.energy<0)throw Error("point FFT missing/repeated input projection");
            const auto k=static_cast<std::size_t>(n-a);projected[k]=p;prefix[k+1]=prefix[k]+p.energy;
            if(!std::isfinite(prefix[k+1]))throw Error("point FFT energy prefix overflow");++next;
            const auto u=node_of(n)-input_first;if((useful||archive)&&u>=0&&u<extent) {
                moments.add(n,u,p,1,true);
                if(edges){const auto local=static_cast<std::size_t>(std::floor(u));
                    edges->add(tile::add_size(input_first,local),n,static_cast<double>(u-local),p);}
            }
        });
        if(next!=b)throw Error("point FFT physical tile incomplete");if(edges)edges->finish(stop);moments.finish();
        if(archive)archive->install_moments(input_first,moments.rows(),moments.energy(),moments.count(),stop);
        if(useful)engine.append(moments.rows(),rows,stop);else ++out.work.skipped_fragments;
        for(std::size_t j=0;j<g.outputs;++j) {
            check_stop();const auto global=tile::add_size(g.origin_first,j),shift=tile::multiply_size(global,g.step);
            const auto domain_first=lower(offset+shift,a,b),domain_end=lower(offset+actual+shift,a,b);
            intersections(selections[j],domain_first,domain_end,[&](auto first,auto end){
                if(end-first>UINT64_MAX-counts[j])throw Error("point FFT observation overflow");
                counts[j]+=end-first;out.work.selected_rows+=end-first;
                energies[j]+=prefix[static_cast<std::size_t>(end-a)]-prefix[static_cast<std::size_t>(first-a)];
            });
            const auto adjust=[&](std::uint64_t first,std::uint64_t end,bool unsupported){
                if(unsupported)++out.work.original_runs;else ++out.work.mask_runs;
                for(auto n=first;n<end;++n) {
                    auto&work=unsupported?out.work.original_rows:out.work.mask_rows;
                    const auto limit=unsupported?limits.maximum_original_rows:limits.maximum_mask_rows;
                    if(work>=limit)throw Error("point FFT corrective work cap reached");if((work++&255U)==0)check_stop();
                    const auto u=node_of(n)-shift;const auto k=static_cast<std::size_t>(std::floor(u));
                    if(k<offset||k-offset>=actual)throw Error("point FFT correction outside template");
                    const auto&c=cells[k-offset].chord;Pair q{};
                    if(unsupported) {
                        const auto row=original(global,n);if(!row.observed)throw Error("point FFT selection/Original disagreement");q=row.q;
                    }else {
                        const auto t=static_cast<double>(u-k);for(unsigned bit=0;bit<2;++bit)q[bit]=(1-t)*c.left[bit]+t*c.right[bit];
                    }
                    for(unsigned bit=0;bit<2;++bit) {
                        if(!std::isfinite(q[bit].real())||!std::isfinite(q[bit].imag()))throw Error("point FFT correction q nonfinite");
                        CorrelationFit f;f.add(projected[static_cast<std::size_t>(n-a)],q[bit],1);
                        if(!events::finite_fit(f))throw Error("point FFT correction fields invalid");
                        const auto sign=unsupported?1.:-1.;auto&d=correction[j][bit];
                        d.xc+=sign*f.xc;d.xs+=sign*f.xs;d.cc+=sign*f.cc;d.ss+=sign*f.ss;d.cs+=sign*f.cs;
                    }
                }
            };
            for(const auto&run:runs) {
                const auto first=lower(offset+run.first+shift,a,b),end=lower(offset+run.end+shift,a,b);
                if(first==end)continue;
                if(run.unsupported)intersections(selections[j],first,end,[&](auto lo,auto hi){adjust(lo,hi,true);});
                else {
                    auto cursor=first;
                    intersections(selections[j],first,end,[&](auto lo,auto hi){if(cursor<lo)adjust(cursor,lo,false);cursor=hi;});
                    if(cursor<end)adjust(cursor,end,false);
                }
            }
        }
        const auto live=out.working_bytes()+engine.working_bytes()+moments.working_bytes()+
            correction.capacity()*sizeof(correction[0])+energies.capacity()*sizeof(energies[0])+counts.capacity()*sizeof(counts[0])+
            cells.capacity()*sizeof(cells[0])+rows.capacity()*sizeof(rows[0])+runs.capacity()*sizeof(runs[0])+
            projected.capacity()*sizeof(projected[0])+prefix.capacity()*sizeof(prefix[0])+4096;
        out.work.peak_bytes=std::max(out.work.peak_bytes,live);if(live>needed)throw Error("point FFT live workspace exceeds preflight");
        offset+=actual;
    }
    const auto transformed=engine.finish(stop);out.work.transforms=engine.work();
    for(std::size_t j=0;j<g.outputs;++j) {
        check_stop();if(counts[j]!=selections[j].count)throw Error("point FFT template does not cover full Original observation domain");
        const auto e=static_cast<double>(energies[j]);if(!std::isfinite(e)||e<0)throw Error("point FFT final energy invalid");
        out.whole[j]=grid::fit(transformed[j],e,counts[j]);bool valid=true;
        for(unsigned bit=0;bit<2;++bit) {
            auto&f=out.whole[j][bit];const auto&d=correction[j][bit];
            f.xc+=d.xc;f.xs+=d.xs;f.cc+=d.cc;f.ss+=d.ss;f.cs+=d.cs;
            valid=valid&&usable(f);
        }
        out.valid[j]=static_cast<std::uint8_t>(valid);
    }
    check_stop();if(archive)archive->seal(stop);check_stop();return out;
}
} // namespace datapump::modem::detail::scheduled_default_point_family
