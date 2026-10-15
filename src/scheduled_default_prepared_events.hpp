#pragma once
// Same Default chord statistics/guards as events::run, borrowing sealed native
// cell/moment centers from a whole-FFT family. No Original certificate or
// admission decision is produced. The owner must bind immutable source, raw
// stream, physical carrier/restart and processing lineage before borrowing.
#include "scheduled_default_point_guard_archive.hpp"
#include "scheduled_default_point_guard_strip.hpp"

namespace datapump::modem::detail::scheduled_default_prepared_events {
namespace events=scheduled_default_linear_events;
namespace archive=scheduled_default_point_guard_archive;
namespace point=scheduled_default_linear_cellstats;
namespace fft=scheduled_linear_grid_fft;
namespace strip=scheduled_default_point_guard_archive;
struct StripWork {
    std::uint64_t attempts=0,recovered_cells=0,bulk_interiors=0,edge_rows=0,
        projection_requests_avoided=0,coverage_fallbacks=0,numeric_fallbacks=0;
};
using events::Cell;using events::Partition;using events::Pair;using events::Complex;
using events::no_owner;using events::add;using events::window_at;using events::section_at;
using events::finite_fit;using events::observed_begin;
inline std::size_t reservation(const events::Geometry& g) {
    fft::require(events::valid_geometry(g),"Default prepared invalid geometry");
    // No PCM/projection tile or Moments is retained. Two public-pulse tables
    // and bounded loop metadata are charged in addition to retained outputs.
    return fft::add(events::retained_reservation(g.outputs),8192);
}
// Projection(n) returns the exact physical projection at n using the Original
// raw-fence/restart convention. It is invoked ONLY for actually observed row
// partitions; a caller-owned bounded scratch may service those requests.
// Classify/Original/Coefficients retain the events::run global-lane contract.
template<class Projection,class Classify,class Original,class Coefficients>
events::Result run(const events::Geometry& g,std::size_t budget,const archive::Archive& prepared,
        Projection&& projection,Classify&& classify,Original&& original,
        Coefficients&& coefficients,std::stop_token stop={},StripWork* strip_work=nullptr) {
    fft::require(events::valid_geometry(g)&&scheduled_acquisition_certificate::environment<double>(),"Default prepared invalid geometry/environment");
    const auto needed=scheduled_default_prepared_events::reservation(g);fft::require(needed<=budget,"Default prepared workspace preflight");
    fft::require(prepared.accepts(g),"Default prepared archive geometry mismatch");
    events::Result out;out.events.resize(g.outputs);
    fft::require(fft::add(out.working_bytes(),8192)<=needed,"Default prepared retained allocation exceeds reservation");
    const auto physical=[&](std::size_t node){const auto x=g.physical_base+static_cast<long double>(node)*g.node_spacing;
        fft::require(std::isfinite(x)&&x>=-0x1p40L&&x<=0x1p41L,"Default prepared physical extent");return x;};
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
    const auto add_row=[&](events::EventStats& target,std::size_t j,const Cell& c,
            const events::Row& row,const CorrelationProjection& p,double t,std::uint64_t n) {
        fft::require(row.observed&&std::isfinite(row.source)&&row.source>=0&&row.source<g.symbol_samples&&
            row.owner==static_cast<std::uint64_t>(std::floor(row.source/g.chip_samples)),"Default prepared original row owner mismatch");
        Pair q=row.q;
        if(c.chord.supported)for(unsigned bit=0;bit<2;++bit)q[bit]=(1-t)*c.chord.left[bit]+t*c.chord.right[bit];
        else ++out.work.source_rows;
        std::array<CorrelationFit,2> fits;
        for(unsigned bit=0;bit<2;++bit){fft::require(scheduled_acquisition_certificate::finite(q[bit]),"Default prepared nonfinite row template");fits[bit].add(p,q[bit],1);}
        const Partition part{true,true,row.owner,window_at(g,row.source),section_at(g,row.source)};
        add(target,g,j,part,fits,n);++out.work.direct_selected_rows;
        if(g.outer) {
            const Complex measured{p.xc,p.xs};
            if(c.chord.supported)pulse(c,[&](std::uint64_t chip,double l,double r){target.outer.add(chip,((1-t)*l+t*r)*measured,coefficients);++out.work.outer_terms;});
            else pattern_pulse_each(static_cast<double>(row.source),g.symbol_samples,g.chip_samples,[&](std::uint64_t chip,double weight){
                fft::require(std::isfinite(row.gain)&&row.gain>=0&&row.gain<=scheduled_cold_colored::smooth_gain_upper(),"Default prepared Original outer gain invalid");
                target.outer.add(chip,(row.gain*weight)*measured,coefficients);++out.work.outer_terms;
            });
        }
    };
    for(std::size_t offset=0;offset<g.template_cells;) {
        fft::stop(stop);const auto actual=std::min(g.fragment_cells,g.template_cells-offset);
        for(std::size_t i=0;i<actual;++i) {
            if((i&63U)==0)fft::stop(stop);
            const auto* cell=prepared.cell(offset+i);fft::require(cell,"Default prepared missing global chord");
            ++out.work.template_cells;
            if(cell->chord.supported) {
                if(cell->chord.observed)++out.work.supported_cells;else ++out.work.quiet_cells;
            }else ++out.work.unsupported_cells;
        }
        for(std::size_t j=0;j<g.outputs;++j) {
            fft::stop(stop);auto& target=out.events[j];const auto global=fft::add(g.origin_first,j),shift=fft::mul(global,g.step);
            for(std::size_t i=0;i<actual;++i) {
                if((i&63U)==0)fft::stop(stop);const auto& c=*prepared.cell(offset+i);
                if(c.chord.supported&&!c.chord.observed)continue;
                const auto at=fft::add(offset+i,shift);const auto [a,b]=bounds(at,at+1);
                // Invert the SAME executed node_of expression used by the
                // producer. Multiplying an endpoint then taking ceil may
                // choose the other side of an exactly sampled boundary.
                const auto lower=[&](long double threshold){auto lo=a,hi=b;while(lo<hi){const auto mid=lo+(hi-lo)/2;
                    if(node_of(mid)<threshold)lo=mid+1;else hi=mid;}return lo;};
                const auto first=lower(at),end=lower(static_cast<long double>(at)+1);
                const auto partition=c.chord.supported?classify(global,first,end):Partition{};
                if(c.chord.supported&&partition.valid) {
                    if(!partition.observed||first==end)continue;
                    const auto moment=prepared.moment(at);
                    // A covering capture can contain future/past rows. Both
                    // cell intervals are nested in that same grid; equal N
                    // proves exactly this requested integer membership.
                    const auto fit=moment.row&&moment.count==end-first?
                        point::contract_centers(*moment.row,c.chord,static_cast<double>(moment.energy),moment.count):point::CellStats{};
                    if(fit.valid) {
                        add(target,g,j,partition,fit.fits,first);++out.work.cell_contractions;
                        if(g.outer)pulse(c,[&](std::uint64_t chip,double l,double r){
                            target.outer.add(chip,l*(*moment.row)[0]+r*(*moment.row)[1],coefficients);++out.work.outer_terms;
                        });continue;
                    }
                }
                // Recover only exact small end strips. Interior membership is
                // certified by the unchanged Original classifier; no row or
                // gain/owner uncertainty is invented from the proxy grid.
                if(c.chord.supported&&prepared.edge_width()) {
                    if(strip_work)++strip_work->attempts;
                    const auto moment=prepared.moment(at);const auto* saved=prepared.edge(at);
                    bool recovered=false,numerical=false;
                    if(moment.row&&saved&&first>=saved->first&&end<=saved->end) {
                        const auto find_edge=[&](std::uint64_t n)->const archive::EdgeRow* {
                            for(unsigned k=0;k<saved->head_count;++k)if(saved->head[k].physical==n)return &saved->head[k];
                            for(unsigned k=0;k<saved->tail_count;++k)if(saved->tail[k].physical==n)return &saved->tail[k];
                            return nullptr;
                        };
                        const auto width=prepared.edge_width();
                        for(unsigned removed=0;removed<=2*width&&!recovered;++removed)for(int left=static_cast<int>(std::min(width,removed));left>=0&&!recovered;--left) {
                            const auto right=removed-static_cast<unsigned>(left);if(right>width)continue;
                            if(static_cast<std::uint64_t>(left)>end-first||right>end-first-static_cast<unsigned>(left))continue;
                            const auto middle_first=first+left,middle_end=end-right;
                            if(middle_end-middle_first<=2)continue;
                            // Capture cropping ALSO consumes retained edge rows.
                            if(middle_first-saved->first>width||saved->end-middle_end>width)continue;
                            if(!removed&&!partition.valid)continue;
                            const auto middle=!removed?partition:classify(global,middle_first,middle_end);
                            if(!middle.valid)continue;
                            std::array<const archive::EdgeRow*,4> edge{};std::array<events::Row,4> exact{};
                            unsigned edges=0;bool available=true;
                            const auto retain_edge=[&](std::uint64_t n) {
                                const auto* row=find_edge(n);if(!row){available=false;return;}
                                edge[edges]=row;exact[edges]=original(global,n,false);++edges;
                            };
                            for(auto n=first;n<middle_first;++n)retain_edge(n);
                            for(auto n=middle_end;n<end;++n)retain_edge(n);
                            if(!available)continue;
                            strip::Stripped remainder;point::CellStats interior;
                            if(middle.observed) {
                                remainder=strip::strip(moment,*saved,middle_first,middle_end,prepared.layout(),at);
                                if(!remainder.known){numerical=true;continue;}
                                interior=point::contract(remainder.row,c.chord,static_cast<double>(remainder.energy),remainder.count);
                                if(!interior.valid||interior.needs_refinement){numerical=true;continue;}
                            }
                            // No EventStats is mutated until every selected
                            // endpoint, interior proof and native numeric gate
                            // needed by this candidate strip are available.
                            for(unsigned k=0;k<edges;++k)if(exact[k].observed)
                                fft::require(std::isfinite(exact[k].source)&&exact[k].source>=0&&exact[k].source<g.symbol_samples&&
                                    exact[k].owner==static_cast<std::uint64_t>(std::floor(exact[k].source/g.chip_samples)),"Default prepared edge owner mismatch");
                            const auto emit_edge=[&](unsigned k) {
                                if(!exact[k].observed)return;
                                add_row(target,j,c,exact[k],edge[k]->projection,edge[k]->t,edge[k]->physical);
                                if(strip_work){++strip_work->edge_rows;++strip_work->projection_requests_avoided;}
                            };
                            for(unsigned k=0;k<static_cast<unsigned>(left);++k)emit_edge(k);
                            if(middle.observed) {
                                add(target,g,j,middle,interior.fits,middle_first);++out.work.cell_contractions;
                                if(strip_work){++strip_work->bulk_interiors;strip_work->projection_requests_avoided+=remainder.count;}
                                if(g.outer)pulse(c,[&](std::uint64_t chip,double l,double r){
                                    target.outer.add(chip,l*remainder.row[0]+r*remainder.row[1],coefficients);++out.work.outer_terms;
                                });
                            }
                            for(unsigned k=static_cast<unsigned>(left);k<edges;++k)emit_edge(k);
                            recovered=true;if(strip_work)++strip_work->recovered_cells;
                        }
                    }
                    if(recovered)continue;
                    if(strip_work){if(numerical)++strip_work->numeric_fallbacks;else ++strip_work->coverage_fallbacks;}
                }
                ++out.work.partitioned_cells;
                for(auto n=first;n<end;++n) {
                    if((n&255U)==0)fft::stop(stop);
                    const auto row=original(global,n,!c.chord.supported);if(!row.observed)continue;
                    // Metadata selection precedes any projection request.
                    const auto p=projection(n);++out.work.projection_lookups;
                    add_row(target,j,c,row,p,static_cast<double>(node_of(n)-at),n);
                }
            }
        }
        offset+=actual;
    }
    out.work.peak_bytes=fft::add(out.working_bytes(),8192);
    for(std::size_t j=0;j<g.outputs;++j) {
        fft::stop(stop);auto& event=out.events[j];
        if(g.differential_width)for(auto& d:event.differential)if(d.initialized)
            d.expected_count=static_cast<long double>(d.window_index)*g.differential_width>=observed_begin(g,j)?d.active.count:0;
        if(g.outer){event.outer.finish(coefficients);for(unsigned bit=0;bit<2;++bit)
            fft::require(std::isfinite(event.outer.dot[bit].real())&&std::isfinite(event.outer.dot[bit].imag())&&
                std::isfinite(event.outer.variance[bit])&&event.outer.variance[bit]>=0,"Default linear completed outer fields invalid");}
        event.valid=event.whole[0].count&&event.whole[0].count==event.whole[1].count&&finite_fit(event.whole[0])&&finite_fit(event.whole[1]);
    }
    out.valid=std::all_of(out.events.begin(),out.events.end(),[](const auto& e){return e.valid;});
    fft::require(scheduled_acquisition_certificate::environment<double>(),"Default prepared environment changed");fft::stop(stop);return out;
}
} // namespace datapump::modem::detail::scheduled_default_prepared_events
