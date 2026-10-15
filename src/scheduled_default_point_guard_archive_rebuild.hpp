#pragma once
// Reconstruct ONE optional current-family archive from the same chosen-point
// cell and Original raw-fence input producers. No FFT, PendingBulk, future
// processing Piece, observation or detector decision is constructed here.
#include "scheduled_default_point_family.hpp"
#include "scheduled_default_point_guard_archive.hpp"
#include <vector>
namespace datapump::modem::detail::scheduled_default_point_guard_archive_rebuild {
namespace family=scheduled_default_point_family;
namespace storage=scheduled_default_point_guard_archive;
namespace events=scheduled_default_linear_events;
namespace point=scheduled_default_linear_cellstats;
namespace tile=scheduled_acquisition_tile;
struct Work {std::uint64_t input_rows=0,cells=0,fragments=0;std::size_t peak_bytes=0;};
inline std::size_t reservation(const family::Geometry& g) {
    if(!family::valid_geometry(g))throw Error("guard archive rebuild geometry invalid");
    const auto fragment=std::min(g.fragment_cells,g.template_cells);
    const auto extent=tile::add_size(fragment,tile::multiply_size(g.outputs-1,g.step));
    return tile::add_size(8192,tile::add_size(tile::multiply_size(fragment,sizeof(events::Cell)),point::Moments::reservation(extent)));
}
template<class Cell,class Visit>
Work run(const family::Geometry& g,std::size_t budget,storage::Archive& archive,
         Cell&& make_cell,Visit&& visit,std::stop_token stop={}) {
    const auto needed=reservation(g);if(needed>budget)throw Error("guard archive rebuild workspace unavailable");
    const auto& l=archive.layout();
    if(l.physical_base!=g.physical_base||l.node_spacing!=g.node_spacing||l.capture_first!=g.capture_first||l.capture_end!=g.capture_end||
       l.template_cells!=g.template_cells||l.origin_first!=g.origin_first||l.outputs!=g.outputs||l.step!=g.step||archive.edge_width())
        throw Error("guard archive rebuild identity/width mismatch");
    const auto fragment=std::min(g.fragment_cells,g.template_cells),halo=tile::multiply_size(g.outputs-1,g.step);
    const auto first_shift=tile::multiply_size(g.origin_first,g.step);
    std::vector<events::Cell> cells;cells.reserve(fragment);
    struct Clean {std::vector<events::Cell>& c;~Clean(){if(c.data())OPENSSL_cleanse(c.data(),c.capacity()*sizeof(c[0]));}}clean{cells};
    const auto physical=[&](std::size_t k){const auto p=g.physical_base+static_cast<long double>(k)*g.node_spacing;
        if(!std::isfinite(p)||p< -0x1p40L||p>0x1p41L)throw Error("guard archive rebuild physical extent");return p;};
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;};
    Work out;
    for(std::size_t offset=0;offset<g.template_cells;) {
        scheduled_linear_grid_fft::stop(stop);const auto actual=std::min(fragment,g.template_cells-offset);cells.clear();
        for(std::size_t j=0;j<actual;++j) {
            if((j&63U)==0)scheduled_linear_grid_fft::stop(stop);
            auto c=make_cell(offset+j,physical(offset+j),physical(offset+j+1));
            // SAME normalization as bulk::prepare. A quiet reference cell is a
            // zero template, not proof that every shifted candidate lacks noise.
            if(c.chord.supported&&!c.chord.observed){c.chord.left={};c.chord.right={};c.chord.observed=true;}
            cells.push_back(std::move(c));++out.cells;
        }
        archive.install_cells(offset,cells,stop);
        const auto input_first=tile::add_size(offset,first_shift),extent=tile::add_size(actual,halo);
        const auto a=static_cast<std::uint64_t>(std::clamp(std::floor(physical(input_first))-2,
            static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)));
        const auto b=static_cast<std::uint64_t>(std::clamp(std::ceil(physical(tile::add_size(input_first,extent)))+2,
            static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)));
        auto next=a;point::Moments moments(extent,point::Moments::reservation(extent));
        if(a<b)visit(a,b,[&](std::uint64_t n,const CorrelationProjection& projection) {
            if((out.input_rows++&255U)==0)scheduled_linear_grid_fft::stop(stop);
            if(n!=next||n>=b)throw Error("guard archive rebuild missing/repeated raw row");++next;
            const auto u=node_of(n)-input_first;
            if(u>=0&&u<extent)moments.add(n,u,projection,1,true);
        });
        if(next!=b)throw Error("guard archive rebuild incomplete raw input");
        moments.finish();
        // Every installed cell is fully covered by the outward-bounded visit,
        // clipped only to the declared actual captured interval. Zero counts
        // therefore mean proved no rows in that interval, never an unvisited
        // zero-filled scratch cell.
        archive.install_moments(input_first,moments.rows(),moments.energy(),moments.count(),stop);
        const auto live=cells.capacity()*sizeof(cells[0])+moments.working_bytes()+8192;
        out.peak_bytes=std::max(out.peak_bytes,live);if(live>budget)throw Error("guard archive rebuild actual workspace exceeded");
        ++out.fragments;offset+=actual;
    }
    scheduled_linear_grid_fft::stop(stop);archive.seal(stop);return out;
}
} // namespace datapump::modem::detail::scheduled_default_point_guard_archive_rebuild
