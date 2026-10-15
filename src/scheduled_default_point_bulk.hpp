#pragma once
// Default chosen-chord arithmetic only. Capture-complete bulk fields are
// explicitly PENDING: they contain no observed energy/count, rank or score.
// Actual processing Pieces and Selection are consulted only by finalize_lane.
#include "scheduled_default_point_family.hpp"
#include "scheduled_default_compact_chords.hpp"
#include <optional>
#include <type_traits>
namespace datapump::modem::detail::scheduled_default_point_bulk {
namespace family=scheduled_default_point_family;
namespace fft=scheduled_default_point_fft;
namespace point=scheduled_default_linear_cellstats;
namespace events=scheduled_default_linear_events;
namespace observation=scheduled_default_point_observation;
namespace grid=scheduled_linear_grid;
namespace tile=scheduled_acquisition_tile;
using Geometry=family::Geometry;
using Pair=family::Pair;
using Fields=std::array<std::array<double,5>,2>; // xc,xs,cc,ss,cs; NEVER E/N
namespace compact=scheduled_default_compact_chords;
using Chord=compact::Chord;
enum class Retention {dense,recipe};
struct MissingResolver {Chord operator()(std::size_t)const {throw Error("compact point chord resolver unavailable");}};
inline bool nonzero(const Chord& c){return c.left[0]!=Pair::value_type{}||c.left[1]!=Pair::value_type{}||
    c.right[0]!=Pair::value_type{}||c.right[1]!=Pair::value_type{};}
struct PendingBulk {
    Geometry geometry;
    std::vector<Fields> fields;
    std::vector<Chord> chords; // retained diagnostic dense backend
    std::unique_ptr<compact::Directory> directory; // recipe backend: no endpoint payloads
    family::Work work;
    bool complete=false; // numerical preparation, NEVER an admission-ready flag
    PendingBulk()=default;PendingBulk(const PendingBulk&)=delete;PendingBulk& operator=(const PendingBulk&)=delete;
    PendingBulk(PendingBulk&&)=default;PendingBulk& operator=(PendingBulk&&)=delete;
    ~PendingBulk(){if(fields.data())OPENSSL_cleanse(fields.data(),fields.capacity()*sizeof(fields[0]));
        if(chords.data())OPENSSL_cleanse(chords.data(),chords.capacity()*sizeof(chords[0]));}
    std::size_t working_bytes()const{return sizeof(*this)+4096+fields.capacity()*sizeof(fields[0])+chords.capacity()*sizeof(chords[0])+(directory?directory->working_bytes():0);}
    static std::size_t reservation(const Geometry& g,Retention retention=Retention::dense){if((retention!=Retention::dense&&retention!=Retention::recipe)||!family::valid_geometry(g))throw Error("pending point bulk invalid geometry");
        return tile::add_size(sizeof(PendingBulk)+4096,tile::add_size(tile::multiply_size(g.outputs,sizeof(Fields)),
            retention==Retention::dense?tile::multiply_size(g.template_cells,sizeof(Chord)):compact::Directory::reservation(g.template_cells)));}
    std::size_t stored_cells()const{return directory?directory->size():chords.size();}
    compact::Classification classification(std::size_t k)const {
        if(directory)return directory->classification(k);
        if(k>=chords.size())throw Error("pending point chord index outside storage");
        const auto& c=chords[k];return {c.supported,scheduled_default_point_bulk::nonzero(c)};
    }
    template<class Resolve>Chord resolve(std::size_t k,Resolve&& provider)const {
        if(!directory){if(k>=chords.size())throw Error("pending point chord index outside storage");return chords[k];}
        const auto kind=directory->kind(k);
        if(kind==compact::Kind::unsupported)return {};
        if(kind==compact::Kind::zero){Chord c;c.supported=true;return c;}
        auto c=provider(k);
        // The provider is bound to a frozen source/context identity. This gate
        // also rejects changed support/zero decisions; it is not a hash proof
        // of arbitrary caller-supplied endpoint data.
        if(compact::classify(c)!=kind)throw Error("pending point regenerated chord classification changed");
        return c;
    }
};
// A fully captured LAST padded aperture makes every preceding output immutable
// under later append. This uses the SAME affine physical endpoint as the
// executed node_of/bounds producer, with the existing two-row outward reserve.
// A caller may trim requested outputs to this monotone prefix before prepare.
inline std::size_t capture_complete_prefix(const Geometry& g){
    if(!family::valid_geometry(g))return 0;std::size_t lo=0,hi=g.outputs;
    while(lo<hi){const auto mid=lo+(hi-lo)/2,global=tile::add_size(g.origin_first,mid);
        const auto node=tile::add_size(g.template_cells,tile::multiply_size(global,g.step));
        const auto right=g.physical_base+static_cast<long double>(node)*g.node_spacing;
        if(std::isfinite(right)&&right>=-0x1p40L&&right<=0x1p41L&&std::ceil(right)+2<=g.capture_end)lo=mid+1;else hi=mid;}
    return lo;
}
template<class T>std::size_t reservation(const Geometry& g,Retention retention=Retention::dense){return tile::add_size(PendingBulk::reservation(g,retention),family::reservation<T>(g));}
struct NoArchive {};
template<class T,class Cell,class Visit,class Archive=NoArchive>
PendingBulk prepare(const Geometry& g,std::size_t budget,Cell&& make_cell,Visit&& visit,
                    std::stop_token stop={},Archive* archive=nullptr,scheduled_default_point_template_cache::Pool<T>* template_pool=nullptr,Retention retention=Retention::dense){
    if(!family::valid_geometry(g)||capture_complete_prefix(g)!=g.outputs)throw Error("pending point bulk aperture is not fully captured");
    const auto needed=scheduled_default_point_bulk::reservation<T>(g,retention);if(needed>budget)throw Error("pending point bulk workspace preflight");
    const auto fragment=std::min(g.fragment_cells,g.template_cells),halo=tile::multiply_size(g.outputs-1,g.step);
    const auto first_origin=tile::multiply_size(g.origin_first,g.step);
    const fft::Geometry fg{fragment,g.outputs,g.step,fragment,true,false};
    fft::StreamingEngine<T> engine(fg,fft::StreamingEngine<T>::reservation(fg),stop,template_pool);
    PendingBulk out;out.geometry=g;out.fields.resize(g.outputs);
    if(retention==Retention::recipe)out.directory=std::make_unique<compact::Directory>(g.template_cells,compact::Directory::reservation(g.template_cells),stop);
    else out.chords.reserve(g.template_cells);
    std::vector<events::Cell> cells;cells.reserve(fragment);std::vector<fft::TemplateRow> rows;rows.reserve(fragment);
    struct Clean {std::vector<events::Cell>& c;std::vector<fft::TemplateRow>&r;
        ~Clean(){if(c.data())OPENSSL_cleanse(c.data(),c.capacity()*sizeof(c[0]));if(r.data())OPENSSL_cleanse(r.data(),r.capacity()*sizeof(r[0]));}}clean{cells,rows};
    const auto physical=[&](std::size_t n){const auto p=g.physical_base+static_cast<long double>(n)*g.node_spacing;
        if(!std::isfinite(p)||p< -0x1p40L||p>0x1p41L)throw Error("pending point bulk physical extent");return p;};
    const auto bounds=[&](std::size_t a,std::size_t b){return std::pair{
        static_cast<std::uint64_t>(std::clamp(std::floor(physical(a))-2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end))),
        static_cast<std::uint64_t>(std::clamp(std::ceil(physical(b))+2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)))};};
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;};
    for(std::size_t offset=0;offset<g.template_cells;){
        fft::stop(stop);const auto actual=std::min(fragment,g.template_cells-offset);cells.clear();rows.clear();bool useful=false;
        for(std::size_t j=0;j<actual;++j){if((j&63U)==0)fft::stop(stop);
            auto c=make_cell(offset+j,physical(offset+j),physical(offset+j+1));++out.work.template_cells;
            if(c.chord.supported){
                if(!c.chord.observed){c.chord.left={};c.chord.right={};c.chord.observed=true;}
                for(const auto q:{c.chord.left[0],c.chord.left[1],c.chord.right[0],c.chord.right[1]})
                    if(!std::isfinite(q.real())||!std::isfinite(q.imag()))throw Error("pending point bulk nonfinite chord");
                ++out.work.supported_cells;
            }else ++out.work.unsupported_cells;
            Chord stored{c.chord.left,c.chord.right,c.chord.supported};
            if(stored.supported&&scheduled_default_point_bulk::nonzero(stored))useful=true;else if(stored.supported)++out.work.zero_cells;
            if(out.directory)out.directory->append(stored);else out.chords.push_back(stored);
            rows.push_back(grid::row(c.chord));cells.push_back(std::move(c));
        }
        if constexpr(!std::is_same_v<Archive,NoArchive>)if(archive)archive->install_cells(offset,std::span<const events::Cell>(cells),stop);
        bool record_moments=useful;
        if constexpr(!std::is_same_v<Archive,NoArchive>)record_moments=record_moments||archive;
        if(record_moments){
            const auto input_first=tile::add_size(offset,first_origin),extent=tile::add_size(actual,halo);
            const auto[a,b]=bounds(input_first,tile::add_size(input_first,extent));auto next=a;
            point::Moments moments(extent,point::Moments::reservation(extent));
            std::optional<scheduled_default_point_guard_archive::EdgeCapture> edges;
            if constexpr(std::is_same_v<Archive,scheduled_default_point_guard_archive::Archive>)
                if(archive&&archive->edge_width())edges.emplace(*archive,stop);
            if(a<b)visit(a,b,[&](std::uint64_t n,const CorrelationProjection& p){
                if((out.work.shared_input_rows++&255U)==0)fft::stop(stop);
                if(n!=next||n>=b)throw Error("pending point bulk missing/repeated physical row");++next;
                const auto u=node_of(n)-input_first;if(u>=0&&u<extent) {
                    moments.add(n,u,p,1,true);
                    if(edges){const auto local=static_cast<std::size_t>(std::floor(u));
                        edges->add(tile::add_size(input_first,local),n,static_cast<double>(u-local),p);}
                }
            });if(next!=b)throw Error("pending point bulk physical input incomplete");if(edges)edges->finish(stop);moments.finish();
            if constexpr(!std::is_same_v<Archive,NoArchive>)if(archive)archive->install_moments(input_first,moments.rows(),moments.energy(),moments.count(),stop);
            if(useful)engine.append(moments.rows(),rows,stop);else ++out.work.skipped_fragments;
            const auto live=out.working_bytes()+engine.working_bytes()+moments.working_bytes()+cells.capacity()*sizeof(cells[0])+rows.capacity()*sizeof(rows[0])+4096;
            out.work.peak_bytes=std::max(out.work.peak_bytes,live);if(live>needed)throw Error("pending point bulk actual workspace exceeded");
        }else ++out.work.skipped_fragments;
        offset+=actual;
    }
    const auto transformed=engine.finish(stop);out.work.transforms=engine.work();
    for(std::size_t j=0;j<g.outputs;++j){fft::stop(stop);const auto fit=grid::fit(transformed[j],0,0);
        for(unsigned bit=0;bit<2;++bit){const auto&f=fit[bit];out.fields[j][bit]={f.xc,f.xs,f.cc,f.ss,f.cs};
            for(const auto x:out.fields[j][bit])if(!std::isfinite(x))throw Error("pending point bulk nonfinite fields");}}
    if(out.directory)out.directory->seal(stop);fft::stop(stop);out.complete=true;return out;
}
using FinalizedBatch=family::Result;
inline std::size_t finalize_reservation(const Geometry& g){
    if(!family::valid_geometry(g))throw Error("pending point finalize geometry unsupported");
    return tile::add_size(family::retained_reservation(g.outputs),tile::add_size(8192+sizeof(long double),tile::add_size(
        tile::multiply_size(family::projection_capacity(g),sizeof(CorrelationProjection)+sizeof(long double)),
        tile::multiply_size(g.outputs,2*sizeof(CorrelationFit)+sizeof(long double)+sizeof(std::uint64_t)))));
}
template<class Visit,class Original,class Resolve=MissingResolver>
FinalizedBatch finalize_due_batch(const PendingBulk& pending,std::size_t first_origin,std::span<const observation::Selection> selections,
        std::size_t budget,Visit&& visit,Original&& original,std::stop_token stop={},family::Limits limits={},Resolve resolve={}){
    const auto&full=pending.geometry;auto g=full;g.origin_first=first_origin;g.outputs=selections.size();const auto needed=scheduled_default_point_bulk::finalize_reservation(g);
    if(!pending.complete||pending.stored_cells()!=g.template_cells||pending.fields.size()!=full.outputs||
       first_origin<full.origin_first||first_origin-full.origin_first>=full.outputs||g.outputs>full.outputs-(first_origin-full.origin_first)||needed>budget)
        throw Error("pending point finalize state/workspace invalid");
    const auto first_shift=tile::multiply_size(g.origin_first,g.step),halo=tile::multiply_size(g.outputs-1,g.step),fragment=std::min(g.fragment_cells,g.template_cells);
    const auto physical=[&](std::size_t n){const auto x=g.physical_base+static_cast<long double>(n)*g.node_spacing;
        if(!std::isfinite(x)||x< -0x1p40L||x>0x1p41L)throw Error("pending point finalize extent invalid");return x;};
    const auto bounds=[&](std::size_t a,std::size_t b){return std::pair{
        static_cast<std::uint64_t>(std::clamp(std::floor(physical(a))-2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end))),
        static_cast<std::uint64_t>(std::clamp(std::ceil(physical(b))+2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)))};};
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;};
    const auto lower=[&](std::size_t node,std::uint64_t a,std::uint64_t b){while(a<b){const auto m=a+(b-a)/2;
        if(node_of(m)<static_cast<long double>(node))a=m+1;else b=m;}return a;};
    const auto intersections=[&](const observation::Selection& selection,std::uint64_t a,std::uint64_t b,auto emit){
        auto i=std::lower_bound(selection.runs.begin(),selection.runs.end(),a,[](const auto&r,auto n){return r.end<=n;});
        for(;i!=selection.runs.end()&&i->first<b;++i){const auto lo=std::max(a,i->first),hi=std::min(b,i->end);if(lo<hi)emit(lo,hi);}};
    for(const auto&selection:selections){std::uint64_t selected_count=0,last=0;
        if(!selection.valid)throw Error("pending point finalize selection unavailable");
        for(const auto&r:selection.runs){if(r.first>=r.end||r.first<g.capture_first||r.end>g.capture_end||r.first<last||r.end-r.first>UINT64_MAX-selected_count)
                throw Error("pending point finalize selection invalid");selected_count+=r.end-r.first;last=r.end;}
        if(selected_count!=selection.count)throw Error("pending point finalize selection count mismatch");}
    FinalizedBatch out;out.whole.resize(g.outputs);out.valid.resize(g.outputs);
    std::vector<std::array<CorrelationFit,2>> corrections(g.outputs);std::vector<std::uint64_t> counts(g.outputs);std::vector<long double> energies(g.outputs);
    std::vector<CorrelationProjection> projected;std::vector<long double> prefix;
    const auto maxrows=family::projection_capacity(g);
    projected.reserve(maxrows);prefix.reserve(maxrows+1);
    struct Clean{std::vector<CorrelationProjection>&p;std::vector<std::array<CorrelationFit,2>>&c;
        ~Clean(){if(p.data())OPENSSL_cleanse(p.data(),p.capacity()*sizeof(p[0]));if(c.data())OPENSSL_cleanse(c.data(),c.capacity()*sizeof(c[0]));}}clean{projected,corrections};
    for(std::size_t offset=0;offset<g.template_cells;){
        fft::stop(stop);const auto actual=std::min(fragment,g.template_cells-offset);
        const auto[a,b]=bounds(offset+first_shift,offset+actual+first_shift+halo);if(b-a>maxrows)throw Error("pending point finalize projection bound exceeded");
        projected.resize(static_cast<std::size_t>(b-a));prefix.resize(projected.size()+1);prefix[0]=0;auto next=a;
        if(a<b)visit(a,b,[&](std::uint64_t n,const CorrelationProjection&p){if((out.work.shared_input_rows++&255U)==0)fft::stop(stop);
            if(n!=next||n>=b||!std::isfinite(p.energy)||p.energy<0)throw Error("pending point finalize input missing/repeated");
            const auto k=static_cast<std::size_t>(n-a);projected[k]=p;prefix[k+1]=prefix[k]+p.energy;
            if(!std::isfinite(prefix[k+1]))throw Error("pending point finalize energy overflow");++next;});
        if(next!=b)throw Error("pending point finalize input incomplete");
        for(std::size_t j=0;j<g.outputs;++j){fft::stop(stop);const auto global=tile::add_size(g.origin_first,j),shift=tile::multiply_size(global,g.step);
        const auto first=lower(offset+shift,a,b),end=lower(offset+actual+shift,a,b);const auto& selection=selections[j];
        intersections(selection,first,end,[&](auto lo,auto hi){if(hi-lo>UINT64_MAX-counts[j])throw Error("pending point finalize observed count overflow");
            counts[j]+=hi-lo;out.work.selected_rows+=hi-lo;energies[j]+=prefix[hi-a]-prefix[lo-a];});
        const auto adjust=[&](std::uint64_t lo,std::uint64_t hi,bool unsupported){
            if(unsupported)++out.work.original_runs;else ++out.work.mask_runs;
            for(auto n=lo;n<hi;++n){auto& work=unsupported?out.work.original_rows:out.work.mask_rows;
                const auto cap=unsupported?limits.maximum_original_rows:limits.maximum_mask_rows;
                if(work>=cap)throw Error("pending point finalize work cap reached");if((work++&255U)==0)fft::stop(stop);
                const auto u=node_of(n)-shift,k=std::floor(u);if(k<offset||k>=offset+actual)throw Error("pending point finalize correction outside cell");
                Pair q;
                if(unsupported){const auto row=original(global,n);if(!row.observed)throw Error("pending point finalize Original/Selection disagreement");q=row.q;}
                else{const auto c=pending.resolve(static_cast<std::size_t>(k),resolve);const auto t=static_cast<double>(u-k);for(unsigned bit=0;bit<2;++bit)q[bit]=(1-t)*c.left[bit]+t*c.right[bit];}
                for(unsigned bit=0;bit<2;++bit){if(!std::isfinite(q[bit].real())||!std::isfinite(q[bit].imag()))throw Error("pending point finalize nonfinite q");
                    CorrelationFit f;f.add(projected[n-a],q[bit],1);if(!events::finite_fit(f))throw Error("pending point finalize invalid correction");
                    const auto sign=unsupported?1.:-1.;auto& d=corrections[j][bit];d.xc+=sign*f.xc;d.xs+=sign*f.xs;d.cc+=sign*f.cc;d.ss+=sign*f.ss;d.cs+=sign*f.cs;}
            }
        };
        // Coalesce adjacent cells of identical correction kind, exactly as
        // the preceding family run, without repeating raw block-prefix mixes.
        for(std::size_t i=0;i<actual;){const auto c=pending.classification(offset+i);
            if(c.supported&&!c.nonzero){++i;continue;}const auto unsupported=!c.supported;auto finish=i+1;
            while(finish<actual){const auto nextc=pending.classification(offset+finish);
                if((!nextc.supported)!=unsupported||(nextc.supported&&!nextc.nonzero))break;++finish;}
            const auto lo=lower(offset+i+shift,a,b),hi=lower(offset+finish+shift,a,b);
            if(unsupported)intersections(selection,lo,hi,[&](auto l,auto r){adjust(l,r,true);});
            else{auto cursor=lo;intersections(selection,lo,hi,[&](auto l,auto r){if(cursor<l)adjust(cursor,l,false);cursor=r;});if(cursor<hi)adjust(cursor,hi,false);}
            i=finish;
        }}
        const auto live=out.working_bytes()+8192+projected.capacity()*sizeof(projected[0])+prefix.capacity()*sizeof(prefix[0])+
            corrections.capacity()*sizeof(corrections[0])+counts.capacity()*sizeof(counts[0])+energies.capacity()*sizeof(energies[0]);
        out.work.peak_bytes=std::max(out.work.peak_bytes,live);if(live>needed)throw Error("pending point finalize actual workspace exceeded");
        offset+=actual;
    }
    for(std::size_t j=0;j<g.outputs;++j){fft::stop(stop);if(counts[j]!=selections[j].count)throw Error("pending point finalize omitted observed domain");
        const auto e=static_cast<double>(energies[j]);if(!std::isfinite(e)||e<0)throw Error("pending point finalize invalid energy");
        bool valid=true;const auto at=g.origin_first+j-full.origin_first;
        for(unsigned bit=0;bit<2;++bit){const auto&f=pending.fields[at][bit];const auto&d=corrections[j][bit];
            out.whole[j][bit]={f[0]+d.xc,f[1]+d.xs,f[2]+d.cc,f[3]+d.ss,f[4]+d.cs,e,counts[j]};valid=valid&&family::usable(out.whole[j][bit]);}
        out.valid[j]=valid;}
    fft::stop(stop);return out;
}
template<class Visit,class Original,class Resolve=MissingResolver>
FinalizedBatch finalize_lane(const PendingBulk& pending,std::size_t global,const observation::Selection& selection,
        std::size_t budget,Visit&& visit,Original&& original,std::stop_token stop={},family::Limits limits={},Resolve resolve={}){
    return finalize_due_batch(pending,global,std::span<const observation::Selection>(&selection,1),budget,
        std::forward<Visit>(visit),std::forward<Original>(original),stop,limits,std::move(resolve));
}
} // namespace datapump::modem::detail::scheduled_default_point_bulk
