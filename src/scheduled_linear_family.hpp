#pragma once
#include "linear_grid_moments.hpp"
#include "streaming_linear_grid_fft.hpp"
#include "scheduled_acquisition_tile.hpp"
#include "scheduled_work_profile.hpp"
// Caller-compatible bounded FAMILY operator. It does not enumerate or exclude
// lanes. Every passed original lane gets a result; its original guards/refine
// path remains mandatory. The producer proves a common exact clock/calendar
// identity and a bounded translation to this node grid.
namespace datapump::modem::detail::scheduled_linear_family {
namespace grid=scheduled_linear_grid;namespace fft=scheduled_linear_grid_fft;
namespace cert=scheduled_acquisition_certificate;namespace tile=scheduled_acquisition_tile;
struct Geometry {
    long double physical_base=0,node_spacing=1;
    std::uint64_t capture_first=0,capture_end=0;
    std::size_t template_cells=0,outputs=0,step=4,fragment_cells=2048;
    // Clipped/nonlattice terminal lanes remain fully Original; they are not
    // dropped and must be covered by the caller's padded physical extent.
    std::span<const std::uint8_t> original_outputs;
};
struct OriginalRow {grid::Pair q{};bool observed=false;};
struct Work {
    fft::Work transforms;
    std::uint64_t template_cells=0,supported_cells=0,quiet_cells=0,original_cells=0,
        input_visits=0,original_visits=0,original_usable=0,skipped_fragments=0,
        original_runs=0,original_run_cells=0;
    std::size_t peak_bytes=0;
};
struct Result {
    std::vector<tile::Value> values;
    double residual_square=0,residual_l1=0,residual_gram=0;
    Work work;
};
inline std::size_t reservation(Geometry g,std::size_t fft_bytes){
    const auto halo=fft::mul(g.outputs-1,g.step),cells=fft::add(g.fragment_cells,halo);
    std::size_t bytes=fft::add(65536,fft_bytes);
    bytes=fft::add(bytes,fft::mul(cells,sizeof(fft::InputRow)+sizeof(fft::InputRadius)+sizeof(long double)+sizeof(cert::Interval)+sizeof(std::uint64_t)));
    bytes=fft::add(bytes,grid::ObservationPrefix::reservation(cells));bytes=fft::add(bytes,grid::ObservationPrefix::run_reservation(g.fragment_cells));
    bytes=fft::add(bytes,fft::mul(g.fragment_cells,sizeof(grid::Cell)+sizeof(fft::TemplateRow)+sizeof(fft::TemplateRadius)));
    return fft::add(bytes,fft::mul(g.outputs,sizeof(tile::Value)+sizeof(grid::Observation)+2*sizeof(CorrelationFit)+2*sizeof(cert::ReferenceErrors)+sizeof(cert::Interval)+2*sizeof(double)));
}
// Cell(k,left,right) returns a supported bounded endpoint chord, an explicit
// quiet cell (supported=true, observed=false), or unsupported. Unsupported
// cells are scored with Original(lane,n) at every actual physical sample.
// Visit(first,end,emit) must supply EACH original captured sample exactly once,
// in increasing physical order, projected with the ORIGINAL physical carrier
// recurrence/fences. It must not reseed at this call's first sample.
// Original() returns the actual original-lane row and usable predicate. It is
// permitted to differ in uncertain boundary cells; that is why they are direct.
template<class T,class Cell,class Visit,class Original>
Result run(Geometry g,std::size_t budget,Cell&& make_cell,Visit&& visit,Original&& original,std::stop_token token={}){
    fft::require(g.outputs&&g.template_cells&&g.fragment_cells&&g.step&&std::isfinite(g.physical_base)&&std::isfinite(g.node_spacing)&&g.node_spacing>0&&
        g.node_spacing<=0x1p32L&&g.capture_first<=g.capture_end&&g.capture_end<=0x10000000000ULL,"linear family invalid geometry");
    fft::require(g.original_outputs.empty()||g.original_outputs.size()==g.outputs,"linear family Original lane extent");
    const auto forced=[&](std::size_t j){return !g.original_outputs.empty()&&g.original_outputs[j]!=0;};
    const auto halo=fft::mul(g.outputs-1,g.step),fragment=std::min(g.fragment_cells,g.template_cells);
    fft::Geometry fg{fragment,g.outputs,g.step,fragment,true,false};
    const auto needed=reservation(g,fft::StreamingEngine<T>::reservation(fg));fft::require(needed<=budget,"linear family workspace preflight");
    fft::StreamingEngine<T> engine(fg,fft::StreamingEngine<T>::reservation(fg),token);
    Result out;out.values.resize(g.outputs);std::vector<grid::Observation> observations(g.outputs);
    std::vector<std::array<CorrelationFit,2>> direct(g.outputs);
    std::vector<cert::Interval> direct_energy(g.outputs);std::vector<std::array<double,2>> direct_trace(g.outputs);
    std::vector<grid::Cell> cells;cells.reserve(fragment);std::vector<fft::TemplateRow> rows;rows.reserve(fragment);
    std::vector<fft::TemplateRadius> radii;radii.reserve(fragment);
    out.work.peak_bytes=fft::add(engine.working_bytes(),fft::mul(g.outputs,sizeof(tile::Value)+sizeof(grid::Observation)+2*sizeof(CorrelationFit)+sizeof(cert::Interval)+2*sizeof(double))+1024);
    const auto physical=[&](long double node){return g.physical_base+node*g.node_spacing;};
    const auto bounds=[&](std::size_t first,std::size_t end){const auto lo=physical(first),hi=physical(end);
        fft::require(std::isfinite(lo)&&std::isfinite(hi)&&lo>=-0x1p40L&&hi<=0x1p41L,"linear family physical extent");
        return std::pair{static_cast<std::uint64_t>(std::clamp(std::floor(lo)-2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end))),
            static_cast<std::uint64_t>(std::clamp(std::ceil(hi)+2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)))};};
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;};
    double residual_square=0,residual_l1=0,residual_gram=0;
    for(std::size_t offset=0;offset<g.template_cells;){fft::stop(token);const auto actual=std::min(fragment,g.template_cells-offset);
        ::datapump::detail::cpu_profile::Scope frontend_cpu(::datapump::detail::cpu_profile::Stage::shared_frontend);
        cells.clear();rows.clear();radii.clear();bool useful=false;std::size_t unsupported=0;
        for(std::size_t i=0;i<actual;++i){if((i&63U)==0)fft::stop(token);auto c=make_cell(offset+i,physical(offset+i),physical(offset+i+1));
            ++out.work.template_cells;if(c.supported){if(c.observed){useful=true;++out.work.supported_cells;
                    for(unsigned b=0;b<2;++b)fft::require(cert::finite(c.left[b])&&cert::finite(c.right[b])&&std::isfinite(c.residual[b])&&c.residual[b]>=0,"linear family invalid cell certificate");
                    const auto e=std::max(c.residual[0],c.residual[1]),q=std::max({cert::norm(c.left[0]),cert::norm(c.left[1]),cert::norm(c.right[0]),cert::norm(c.right[1])});
                    // At most ceil(h)+2 integer physical samples occupy a cell,
                    // uniformly across the original translated output lattice.
                    const auto n=cert::up(static_cast<double>(std::ceil(g.node_spacing)+2));
                    residual_square=cert::add(residual_square,cert::mul(n,cert::mul(e,e)));residual_l1=cert::add(residual_l1,cert::mul(n,e));
                    residual_gram=cert::add(residual_gram,cert::mul(n,cert::add(cert::mul(2*q,e),cert::mul(e,e))));
                }else ++out.work.quiet_cells;
            }else{++unsupported;++out.work.original_cells;}
            rows.push_back(grid::row(c));radii.push_back(grid::product_radii(c));cells.push_back(c);
        }
        if(useful){const auto extent=fft::add(actual,halo);grid::Moments moments(extent,true,needed);
            const auto [first,end]=bounds(offset,fft::add(offset,extent));
            if(first<end)visit(first,end,[&](std::uint64_t n,const CorrelationProjection&p){if((out.work.input_visits++&255U)==0)fft::stop(token);
                const auto u=node_of(n)-offset;if(u>=0&&u<extent)moments.add(n,u,p,1,true);});
            moments.finish();grid::ObservationPrefix prefix(moments,grid::ObservationPrefix::reservation(extent));
            const auto runs=grid::ObservationPrefix::runs(cells,grid::ObservationPrefix::run_reservation(actual));
            for(std::size_t j=0;j<g.outputs;++j){if((j&63U)==0)fft::stop(token);if(forced(j))continue;const auto o=prefix.query(runs,j*g.step);
                observations[j].energy+=o.energy;fft::require(o.count<=UINT64_MAX-observations[j].count,"linear family count overflow");observations[j].count+=o.count;
                observations[j].bounded_energy=cert::plus(observations[j].bounded_energy,o.bounded_energy);}
            engine.append(moments.rows(),rows,moments.radii(),radii,token);
            const auto resident=fft::mul(g.outputs,sizeof(tile::Value)+sizeof(grid::Observation)+2*sizeof(CorrelationFit)+sizeof(cert::Interval)+2*sizeof(double))+1024;
            const auto live=fft::add(resident,fft::add(engine.working_bytes(),fft::add(moments.working_bytes(),fft::add(prefix.working_bytes(),
                fft::add(fft::mul(runs.capacity(),sizeof(grid::ObservationPrefix::Run)),fft::add(fft::mul(cells.capacity(),sizeof(cells[0])),fft::add(fft::mul(rows.capacity(),sizeof(rows[0])),fft::mul(radii.capacity(),sizeof(radii[0])))))))));
            out.work.peak_bytes=std::max(out.work.peak_bytes,live);fft::require(live<=needed,"linear family actual workspace exceeds preflight");
        }else ++out.work.skipped_fragments;
        frontend_cpu.finish();
        ::datapump::detail::cpu_profile::Scope direct_cpu(::datapump::detail::cpu_profile::Stage::scheduled_candidate_residual);
        for(std::size_t j=0;j<g.outputs;++j){if(!unsupported&&!forced(j))continue;fft::stop(token);
            // Consecutive unsupported cells form a single physical interval.
            // Retain their ascending row order, but visit raw carrier fences
            // and the conservative +/-2 padding only once for the whole run.
            // A forced terminal lane is one complete direct run per fragment.
            for(std::size_t i=0;i<actual;){if((i&63U)==0)fft::stop(token);if(cells[i].supported&&!forced(j)){++i;continue;}
                const auto run_first=i++;while(i<actual&&(forced(j)||!cells[i].supported)){if((i&63U)==0)fft::stop(token);++i;}
                const auto at=fft::add(offset+run_first,j*g.step),past=fft::add(offset+i,j*g.step);
                const auto [first,end]=bounds(at,past);++out.work.original_runs;out.work.original_run_cells+=i-run_first;
                if(first<end)visit(first,end,[&](std::uint64_t n,const CorrelationProjection&p){if((out.work.original_visits++&255U)==0)fft::stop(token);const auto u=node_of(n);
                    if(!(u>=at&&u<past))return;const auto q=original(j,n);if(!q.observed)return;
                    fft::require(std::isfinite(p.energy)&&p.energy>=0&&std::isfinite(p.xc)&&std::isfinite(p.xs)&&p.cc>=0&&p.cc<=4&&p.ss>=0&&p.ss<=4&&std::isfinite(p.cs),"linear family Original projection invalid");
                    direct_energy[j]=cert::plus(direct_energy[j],cert::Interval::point(p.energy));
                    for(unsigned b=0;b<2;++b){fft::require(cert::finite(q.q[b]),"linear family Original row nonfinite");direct[j][b].add(p,q.q[b],1);
                        const auto qnorm=cert::norm(q.q[b]);direct_trace[j][b]=cert::add(direct_trace[j][b],cert::mul(cert::add(p.cc,p.ss),cert::mul(qnorm,qnorm)));}++out.work.original_usable;
                    observations[j].bounded_energy=cert::plus(observations[j].bounded_energy,cert::Interval::point(p.energy));});}}
        direct_cpu.finish();offset+=actual;
    }
    const auto transformed=engine.finish(token);out.work.transforms=engine.work();
    for(std::size_t j=0;j<g.outputs;++j){fft::stop(token);const fft::Output raw=forced(j)?fft::Output{}:transformed[j];const auto fits=grid::fit(raw,static_cast<double>(observations[j].energy),observations[j].count);
        auto certificate=grid::fft_certificate(engine.work(),observations[j],raw);
        for(unsigned b=0;b<2;++b){auto f=fits[b];const auto&d=direct[j][b];
            const auto direct_error=cert::scalar_reference_errors(d.count,std::max(0.,direct_energy[j].hi),direct_trace[j][b],0);
            const auto mass=cert::add(cert::add(std::abs(f.xc),std::abs(f.xs)),cert::add(std::abs(d.xc),std::abs(d.xs)));
            const auto gram_mass=cert::add(cert::add(std::abs(f.cc),std::abs(f.ss)),cert::add(cert::add(std::abs(d.cc),std::abs(d.ss)),cert::add(std::abs(f.cs),std::abs(d.cs))));
            certificate.dot_radius[b]=cert::add(certificate.dot_radius[b],cert::add(direct_error.dot,cert::mul(cert::gamma(16,std::numeric_limits<double>::epsilon()/2),mass)));
            certificate.gram_radius[b]=cert::add(certificate.gram_radius[b],cert::add(direct_error.gram,cert::mul(cert::gamma(16,std::numeric_limits<double>::epsilon()/2),gram_mass)));
            certificate.valid=certificate.valid&&direct_error.valid&&std::isfinite(certificate.dot_radius[b])&&std::isfinite(certificate.gram_radius[b]);
            f.xc+=d.xc;f.xs+=d.xs;f.cc+=d.cc;f.ss+=d.ss;f.cs+=d.cs;f.energy+=d.energy;f.count+=d.count;out.values[j].fits[b]=f;
        }
        certificate.energy_lower=std::max(0.,observations[j].bounded_energy.lo);certificate.energy_upper=std::max(0.,observations[j].bounded_energy.hi);
        out.values[j].certificate=certificate;
    }
    out.residual_square=residual_square;out.residual_l1=residual_l1;out.residual_gram=residual_gram;fft::stop(token);return out;
}
} // namespace datapump::modem::detail::scheduled_linear_family
